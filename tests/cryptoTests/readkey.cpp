/******************************************************************************\

          This file is part of the C! library.  A.K.A the cbang library.

                Copyright (c) 2003-2022, Cauldron Development LLC
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

#include <cbang/Catch.h>
#include <cbang/os/SystemUtilities.h>
#include <cbang/openssl/KeyPair.h>

using namespace std;
using namespace cb;


int main(int argc, char *argv[]) {
  try {
    for (int i = 1; i < argc; i++) {
      KeyPair key;
      key.read(*SystemUtilities::open(argv[i]));

      if (key.hasPublic()) {
        cout << "Public:\n";
        key.writePublic(cout);
        cout << endl;
      }

      if (key.hasPrivate()) {
        cout << "Private:\n";
        key.writePrivate(cout);
        cout << endl;
      }
    }

    return 0;
  } CATCH_ERROR;

  return 1;
}
