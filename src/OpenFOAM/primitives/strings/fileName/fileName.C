/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2017 OpenFOAM Foundation
     \\/     M anipulation  | Copyright (C) 2016-2018 OpenCFD Ltd.
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "fileName.H"
#include "wordRe.H"
#include "wordList.H"
#include "DynamicList.H"
#include "OSspecific.H"
#include "fileOperation.H"
#include "stringOps.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

const char* const Foam::fileName::typeName = "fileName";
int Foam::fileName::debug(debug::debugSwitch(fileName::typeName, 0));
const Foam::fileName Foam::fileName::null;


// * * * * * * * * * * * * * Static Member Functions * * * * * * * * * * * * //

Foam::fileName Foam::fileName::validate
(
    const std::string& s,
    const bool doClean
)
{
    fileName out;
    out.resize(s.size());

    char prev = 0;
    std::string::size_type len = 0;

    // Largely as per stripInvalid
    for (auto iter = s.cbegin(); iter != s.cend(); ++iter)
    {
        const char c = *iter;

        if (fileName::valid(c))
        {
            if (doClean && prev == '/' && c == '/')
            {
                // Avoid repeated '/';
                continue;
            }

            // Only track valid chars
            out[len++] = prev = c;
        }
    }

    if (doClean && prev == '/' && len > 1)
    {
        // Avoid trailing '/'
        --len;
    }

    out.resize(len);

    return out;
}


bool Foam::fileName::equals(const std::string& s1, const std::string& s2)
{
    // Do not use (s1 == s2) or s1.compare(s2) first since this would
    // potentially be doing the comparison twice.

    std::string::size_type i1 = 0;
    std::string::size_type i2 = 0;

    const auto n1 = s1.size();
    const auto n2 = s2.size();

    //Info<< "compare " << s1 << " == " << s2 << endl;
    while (i1 < n1 && i2 < n2)
    {
        //Info<< "check '" << s1[i1] << "' vs '" << s2[i2] << "'" << endl;

        if (s1[i1] != s2[i2])
        {
            return false;
        }

        // Increment to next positions and also skip repeated slashes
        do
        {
            ++i1;
        }
        while (s1[i1] == '/');

        do
        {
            ++i2;
        }
        while (s2[i2] == '/');
    }
    //Info<< "return: " << Switch(i1 == n1 && i2 == n2) << endl;

    // Equal if it made it all the way through both strings
    return (i1 == n1 && i2 == n2);
}


bool Foam::fileName::isBackup(const std::string& str)
{
    if (str.empty())
    {
        return false;
    }
    else if (str.back() == '~')
    {
        return true;
    }

    // Now check the extension
    const auto dot = find_ext(str);

    if (dot == npos)
    {
        return false;
    }

    const std::string ending = str.substr(dot+1);

    if (ending.empty())
    {
        return false;
    }

    return
    (
        ending == "bak" || ending == "BAK"
     || ending == "old" || ending == "save"
    );
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::fileName::fileName(const UList<word>& list)
{
    size_type len = 0;
    for (const word& item : list)
    {
        len += 1 + item.length();   // Include space for '/' needed
    }
    reserve(len);

    for (const word& item : list)
    {
        if (item.length())
        {
            if (length()) operator+=('/');
            operator+=(item);
        }
    }
}


Foam::fileName::fileName(std::initializer_list<word> list)
{
    size_type len = 0;
    for (const word& item : list)
    {
        len += 1 + item.length();   // Include space for '/' needed
    }
    reserve(len);

    for (const word& item : list)
    {
        if (item.length())
        {
            if (length()) operator+=('/');
            operator+=(item);
        }
    }
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::fileName::Type Foam::fileName::type
(
    bool followLink,
    bool checkGzip
) const
{
    Type t = ::Foam::type(*this, followLink);

    if (checkGzip && (Type::UNDEFINED == t) && size())
    {
        // Also check for gzip file?
        t = ::Foam::type(*this + ".gz", followLink);
    }

    return t;
}


Foam::fileName& Foam::fileName::toAbsolute()
{
    if (!isAbsolute(*this))
    {
        fileName& f = *this;
        f = cwd()/f;
        f.clean();
    }

    return *this;
}


bool Foam::fileName::clean(std::string& str)
{
    // Start with the top slash found - we are never allowed to go above it
    char prev = '/';
    auto top = str.find(prev);

    // No slashes - nothing to do
    if (top == std::string::npos)
    {
        return false;
    }

    // Number of output characters
    auto nChar = top+1;

    const auto maxLen = str.size();

    for (auto src = nChar; src < maxLen; /*nil*/)
    {
        const char c = str[src++];

        if (prev == '/')
        {
            // Repeated '/' - skip it
            if (c == '/')
            {
                continue;
            }

            // Could be "/./", "/../" or a trailing "/."
            if (c == '.')
            {
                // Trailing "/." - skip it
                if (src >= maxLen)
                {
                    break;
                }

                // Peek at the next character
                const char c1 = str[src];

                // Found "/./" - skip it
                if (c1 == '/')
                {
                    ++src;
                    continue;
                }

                // Trailing "/.." or intermediate "/../"
                if (c1 == '.' && (src+1 >= maxLen || str[src+1] == '/'))
                {
                    string::size_type parent;

                    // Backtrack to find the parent directory
                    // Minimum of 3 characters:  '/x/../'
                    // Strip it, provided it is above the top point
                    if
                    (
                        nChar > 2
                     && (parent = str.rfind('/', nChar-2)) != string::npos
                     && parent >= top
                    )
                    {
                        nChar = parent + 1;   // Retain '/' from the parent
                        src += 2;
                        continue;
                    }

                    // Bad resolution, eg 'abc/../../'
                    // Retain the sequence, but move the top to avoid it being
                    // considered a valid parent later
                    top = nChar + 2;
                }
            }
        }
        str[nChar++] = prev = c;
    }

    // Remove trailing slash
    if (nChar > 1 && str[nChar-1] == '/')
    {
        nChar--;
    }

    str.resize(nChar);

    return (nChar != maxLen);
}


bool Foam::fileName::clean()
{
    return fileName::clean(*this);
}


Foam::fileName Foam::fileName::clean() const
{
    fileName cleaned(*this);
    fileName::clean(cleaned);
    return cleaned;
}


std::string Foam::fileName::nameLessExt(const std::string& str)
{
    auto beg = str.rfind('/');
    auto dot = str.rfind('.');

    if (beg == npos)
    {
        beg = 0;
    }
    else
    {
        ++beg;
    }

    if (dot != npos && dot <= beg)
    {
        dot = npos;
    }

    if (dot == npos)
    {
        return str.substr(beg);
    }

    return str.substr(beg, dot - beg);
}


Foam::fileName Foam::fileName::relative
(
    const fileName& parent,
    const bool caseTag
) const
{
    const auto top = parent.size();
    const fileName& f = *this;

    // Everything after "parent/xxx/yyy" -> "xxx/yyy"
    //
    // case-relative:
    //     "parent/xxx/yyy" -> "<case>/xxx/yyy"
    if
    (
        top && (f.size() > (top+1)) && f[top] == '/'
     && f.startsWith(parent)
    )
    {
        if (caseTag)
        {
            return "<case>"/f.substr(top+1);
        }
        else
        {
            return f.substr(top+1);
        }
    }
    else if (caseTag && f.size() && !f.isAbsolute())
    {
        return "<case>"/f;
    }

    return f;
}


bool Foam::fileName::hasExt(const wordRe& ending) const
{
    return string::hasExt(ending);
}


Foam::wordList Foam::fileName::components(const char delimiter) const
{
    const auto parsed = stringOps::split<string>(*this, delimiter);

    wordList words(parsed.size());

    label i = 0;
    for (const auto& sub : parsed)
    {
        // Could easily filter out '.' here too
        words[i] = sub.str();
        ++i;
    }

    // As a plain wordList
    return words;
}


Foam::word Foam::fileName::component
(
    const size_type cmpt,
    const char delimiter
) const
{
    const auto parsed = stringOps::split<string>(*this, delimiter);

    if (cmpt < parsed.size())
    {
        return parsed[cmpt].str();
    }

    return word();
}


// * * * * * * * * * * * * * * * Member Operators  * * * * * * * * * * * * * //

Foam::fileName& Foam::fileName::operator/=(const string& other)
{
    fileName& s = *this;

    if (s.size())
    {
        if (other.size())
        {
            // Two non-empty strings: can concatenate

            if (s.back() != '/' && other.front() != '/')
            {
                s += '/';
            }

            s.append(other);
        }
    }
    else if (other.size())
    {
        // The first string is empty
        s = other;
    }

    return *this;
}


// * * * * * * * * * * * * * * * Global Operators  * * * * * * * * * * * * * //

Foam::fileName Foam::operator/(const string& a, const string& b)
{
    if (a.size())
    {
        if (b.size())
        {
            // Two non-empty strings: can concatenate

            if (a.back() == '/' || b.front() == '/')
            {
                return fileName(a + b);
            }
            else
            {
                return fileName(a + '/' + b);
            }
        }

        return a;  // The second string was empty
    }

    if (b.size())
    {
        // The first string is empty
        return b;
    }

    // Both strings are empty
    return fileName();
}


// * * * * * * * * * * * * * * * Global Functions  * * * * * * * * * * * * * //

Foam::fileName Foam::search(const word& file, const fileName& directory)
{
    // Search current directory for the file
    for
    (
        const fileName& item
      : fileHandler().readDir(directory, fileName::FILE)
    )
    {
        if (item == file)
        {
            return directory/item;
        }
    }

    // If not found search each of the sub-directories
    for
    (
        const fileName& item
      : fileHandler().readDir(directory, fileName::DIRECTORY)
    )
    {
        fileName path = search(file, directory/item);
        if (!path.empty())
        {
            return path;
        }
    }

    return fileName();
}


// ************************************************************************* //
