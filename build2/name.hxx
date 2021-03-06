// file      : build2/name.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

// Note: include <build2/types.hxx> instead of this file directly.
//

#ifndef BUILD2_NAME_HXX
#define BUILD2_NAME_HXX

// We cannot include <build2/utility.hxx> since it includes <build2/types>.
//
#include <utility> // move()

namespace build2
{
  using std::move;

  // A name is what we operate on by default. Depending on the context, it can
  // be interpreted as a target or prerequisite name. A name without a type
  // and directory can be used to represent any text. A name with directory
  // and empty value represents a directory.
  //
  // A name may also be qualified with a project. If the project name is
  // empty, then it means the name is in a project other than our own (e.g.,
  // it is installed).
  //
  // A type or project can only be specified if either directory or value are
  // not empty.
  //
  // If pair is not '\0', then this name and the next in the list form a
  // pair. Can be used as a bool flag.
  //
  struct name
  {
    optional<project_name> proj;
    dir_path dir;
    string type;
    string value;
    char pair = '\0';

    name () {} // = default; Clang needs this to initialize const object.
    name (string v): value (move (v)) {}
    name (dir_path d): dir (move (d)) {}
    name (string t, string v): type (move (t)), value (move (v)) {}
    name (dir_path d, string v): dir (move (d)), value (move (v)) {}

    name (dir_path d, string t, string v)
        : dir (move (d)), type (move (t)), value (move (v)) {}

    name (optional<project_name> p, dir_path d, string t, string v)
        : proj (move (p)), dir (move (d)), type (move (t)), value (move (v)) {}

    bool
    qualified () const {return proj.has_value ();}

    bool
    unqualified () const {return !qualified ();}

    bool
    typed () const {return !type.empty ();}

    bool
    untyped () const {return type.empty ();}

    // Note: if dir and value are empty then there should be no proj or type.
    //
    bool
    empty () const {return dir.empty () && value.empty ();}

    // Note that strictly speaking the following tests should be orthogonal
    // to qualification. However, the vast majority of cases where we expect
    // a simple or directory name, we also expect it to be unqualified.
    //
    // Note also that empty name is simple but not a directory.
    //
    bool
    simple (bool ignore_qual = false) const
    {
      return (ignore_qual || unqualified ()) && untyped () && dir.empty ();
    }

    bool
    directory (bool ignore_qual = false) const
    {
      return (ignore_qual || unqualified ()) &&
        untyped () && !dir.empty () && value.empty ();
    }

    int
    compare (const name&) const;
  };

  extern const name empty_name;

  inline bool
  operator== (const name& x, const name& y) {return x.compare (y) == 0;}

  inline bool
  operator!= (const name& x, const name& y) {return !(x == y);}

  inline bool
  operator< (const name& x, const name& y) {return x.compare (y) < 0;}

  // Return string representation of a name.
  //
  string
  to_string (const name&);

  // Store a string in a name in a reversible way. If the string ends with a
  // trailing directory separator then it is stored as a directory, otherwise
  // as a simple name.
  //
  name
  to_name (string);

  // Serialize the name to the stream. If requested, the name components
  // containing special characters are quoted. The special characters are:
  //
  // {}[]$() \t\n#\"'%
  //
  // If the pair argument is not '\0', then it is added to the above special
  // characters set. If the quote character is present in the component then
  // it is double quoted rather than single quoted. In this case the following
  // characters are escaped:
  //
  // \$("
  //
  // Note that in the quoted mode empty unqualified name is printed as '',
  // not {}.
  //
  ostream&
  to_stream (ostream&, const name&, bool quote, char pair = '\0');

  inline ostream&
  operator<< (ostream& os, const name& n) {return to_stream (os, n, false);}

  // Vector of names.
  //
  // Quite often it will contain just one element so we use small_vector<1>.
  // Note also that it must be a separate type rather than an alias for
  // vector<name> in order to distinguish between untyped variable values
  // (names) and typed ones (vector<name>).
  //
  using names = small_vector<name, 1>;
  using names_view = vector_view<const name>;

  extern const names empty_names;

  // The same semantics as to_stream(name).
  //
  ostream&
  to_stream (ostream&, const names_view&, bool quote, char pair = '\0');

  inline ostream&
  operator<< (ostream& os, const names_view& ns) {
    return to_stream (os, ns, false);}

  inline ostream&
  operator<< (ostream& os, const names& ns) {return os << names_view (ns);}

  // Pair of names.
  //
  using name_pair = pair<name, name>;
}

#include <build2/name.ixx>

#endif // BUILD2_NAME_HXX
