/******************************************************************************\

          This file is part of the C! library.  A.K.A the cbang library.

                Copyright (c) 2003-2019, Cauldron Development LLC
                   Copyright (c) 2003-2017, Stanford University
                               All rights reserved.

         The C! library is free software: you can redistribute it and/or
        modify it under the terms of the GNU Lesser General Public License
       as published by the Free Software Foundation, either version 2.1 of
               the License, or (at your option) any later version.

        The C! library is distributed in the hope that it will be useful,
          but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
                 Lesser General Public License for more details.

         You should have received a copy of the GNU Lesser General Public
                 License along with the C! library.  If not, see
                         <http://www.gnu.org/licenses/>.

        In addition, BSD licensing may be granted on a case by case basis
        by written permission from at least one of the copyright holders.
           You may request written permission by emailing the authors.

                  For information regarding this software email:
                                 Joseph Coffland
                          joseph@cauldrondevelopment.com

\******************************************************************************/

#include "Regex.h"

#include <boost/regex.hpp>


using namespace cb;
using namespace std;


namespace {
  boost::match_flag_type typeToFormatFlags(Regex::type_t type) {
    switch (type) {
    case Regex::TYPE_POSIX: return boost::format_sed;
    case Regex::TYPE_PERL:  return boost::format_perl;
    case Regex::TYPE_BOOST: return boost::format_default;
    default: THROW("Invalid regex type: " << type);
    }
  }


  boost::match_flag_type typeToMatchFlags(Regex::type_t type) {
    switch (type) {
    case Regex::TYPE_POSIX: return boost::match_posix;
    case Regex::TYPE_PERL:  return boost::match_perl;
    case Regex::TYPE_BOOST: return boost::match_default;
    default: THROW("Invalid regex type: " << type);
    }
  }
}


struct Regex::private_t {
  boost::regex re;

  private_t(const string &pattern) try : re(pattern) {
  } catch (const boost::regex_error &e) {
    THROW("Failed to parse regex: " << e.what());
  }
};


struct Regex::Match::private_t {
  boost::smatch m;
};


Regex::Match::Match(type_t type) : pri(new private_t), type(type) {}


string Regex::Match::format(const std::string &fmt) const {
  try {
    return pri->m.format(fmt, typeToFormatFlags(type));

  } catch (const boost::regex_error &e) {
    THROW("Format error: " << e.what());
  }
}


unsigned Regex::Match::position(unsigned i) const {
  if (size() <= i) THROW("Invalid match subgroup " << i);
  return pri->m.position(i);
}


Regex::Regex(const string &pattern, type_t type) :
  pri(new private_t(pattern)), type(type) {}


string Regex::toString() const {return pri->re.str();}


bool Regex::match(const string &s) const {
  try {
    return boost::regex_match(s, pri->re, typeToMatchFlags(type));

  } catch (const boost::regex_error &e) {
    THROW("Match error: " << e.what());
  }
}


bool Regex::match(const string &s, Match &m) const {
  try {
    if (!boost::regex_match(s, m.pri->m, pri->re, typeToMatchFlags(type)))
      return false;

  } catch (const boost::regex_error &e) {
    THROW("Match error: " << e.what());
  }

  for (unsigned i = 0; i < m.pri->m.size(); i++)
    m.push_back(string(m.pri->m[i].first, m.pri->m[i].second));

  return true;
}


bool Regex::search(const string &s) const {
  try {
    return boost::regex_search(s, pri->re);

  } catch (const boost::regex_error &e) {
    THROW("Search error: " << e.what());
  }
}


bool Regex::search(const string &s, Match &m) const {
  try {
    if (!boost::regex_search(s, m.pri->m, pri->re, typeToMatchFlags(type)))
      return false;

  } catch (const boost::regex_error &e) {
    THROW("Search error: " << e.what());
  }

  for (unsigned i = 0; i < m.pri->m.size(); i++)
    m.push_back(string(m.pri->m[i].first, m.pri->m[i].second));

  return true;
}


string Regex::replace(const string &s, const string &r) const {
  return boost::regex_replace(s, pri->re, r,
                              typeToMatchFlags(type) | typeToFormatFlags(type));
}
