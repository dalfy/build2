// file      : build/target.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2015 Code Synthesis Tools CC
// license   : MIT; see accompanying LICENSE file

#include <build/target>

#include <ostream>

#include <build/context>

using namespace std;

namespace build
{
  // target
  //
  ostream&
  operator<< (ostream& os, const target& t)
  {
    os << t.type ().name << '{';

    if (!t.directory.empty ())
    {
      string s (diagnostic_string (t.directory));

      if (!s.empty ())
        os << s << path::traits::directory_separator;
    }

    os << t.name;

    if (t.ext != nullptr)
      os << '.' << *t.ext;

    os << '}';

    return os;
  }

  target_set targets;
  target* default_target = nullptr;
  target_type_map target_types;

  // path_target
  //
  timestamp path_target::
  load_mtime () const
  {
    assert (!path_.empty ());
    return path_mtime (path_);
  }

  const target_type target::static_type {
    typeid (target), "target", nullptr, nullptr};

  const target_type mtime_target::static_type {
    typeid (mtime_target), "mtime_target", &target::static_type, nullptr};

  const target_type path_target::static_type {
    typeid (path_target), "path_target", &mtime_target::static_type, nullptr};

  const target_type file::static_type {
    typeid (file), "file", &path_target::static_type, &target_factory<file>};
}