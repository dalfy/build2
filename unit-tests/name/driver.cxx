// file      : unit-tests/name/driver.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <sstream>

#include <cassert>
#include <iostream>

#include <build2/types.hxx>   // Includes name.
#include <build2/utility.hxx>

#include <build2/diagnostics.hxx>

using namespace std;

namespace build2
{
  int
  main (int, char*[])
  {
    using dir = dir_path;

    // Test string representation.
    //
    {
      auto ts = [] (const name& n) {return to_string (n);};

      assert (ts (name ()) == "");

      assert (ts (name ("foo")) == "foo");

      assert (ts (name (dir ("bar/")))     == "bar/");
      assert (ts (name (dir ("bar/baz/"))) == "bar/baz/");

      assert (ts (name (dir ("bar/"),     "dir", "")) == "dir{bar/}");
      assert (ts (name (dir ("bar/baz/"), "dir", "")) == "bar/dir{baz/}");

      assert (ts (name (dir ("bar/"), "foo")) == "bar/foo");

      assert (ts (name (dir ("bar/"),     "dir", "foo")) == "bar/dir{foo}");
      assert (ts (name (dir ("bar/baz/"), "dir", "foo")) == "bar/baz/dir{foo}");
    }

    // Test stream representation.
    //
    {
      auto ts = [] (const name& n, bool quote = true)
      {
        ostringstream os;
        stream_verb (os, stream_verbosity (0, 1));
        to_stream (os, n, quote);
        return os.str ();
      };

      assert (ts (name ())        == "''");
      assert (ts (name (), false) == "{}");

      assert (ts (name ("foo")) == "foo");

      assert (ts (name (dir ("bar/")))     == "bar/");
      assert (ts (name (dir ("bar/baz/"))) == "bar/baz/");

      assert (ts (name (dir ("bar/"),     "dir", "")) == "dir{bar/}");
      assert (ts (name (dir ("bar/baz/"), "dir", "")) == "bar/dir{baz/}");

      assert (ts (name (dir ("bar/"), "foo")) == "bar/foo");

      assert (ts (name (dir ("bar/"),     "dir", "foo")) == "bar/dir{foo}");
      assert (ts (name (dir ("bar/baz/"), "dir", "foo")) == "bar/baz/dir{foo}");

      // Quoting.
      //
      assert (ts (name (dir ("bar baz/"), "dir", "foo fox")) == "'bar baz/'dir{'foo fox'}");

      // Relative logic.
      //
#ifndef _WIN32
      dir rb ("/bar/");
      relative_base = &rb;

      assert (ts (name (dir ("/bar/"),     "dir", ""))    == "dir{./}");
      assert (ts (name (dir ("/bar/"),     "",    "foo")) == "foo");
      assert (ts (name (dir ("/bar/baz/"), "dir", ""))    == "dir{baz/}");
#endif
    }

    return 0;
  }
}

int
main (int argc, char* argv[])
{
  return build2::main (argc, argv);
}
