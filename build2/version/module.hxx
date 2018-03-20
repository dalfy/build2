// file      : build2/version/module.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BUILD2_VERSION_MODULE_HXX
#define BUILD2_VERSION_MODULE_HXX

#include <map>

#include <build2/types.hxx>
#include <build2/utility.hxx>

#include <build2/module.hxx>

namespace build2
{
  namespace version
  {
    // The 'depends' values from manifest.
    //
    using dependency_constraints = std::map<string, string>;

    struct module: module_base
    {
      static const string name;

      butl::standard_version version;
      bool committed; // Whether this is a committed snapshot.

      dependency_constraints dependencies;

      const variable* in_symbol       = nullptr; // in.symbol
      const variable* in_substitution = nullptr; // in.substitution

      module (butl::standard_version v,
              bool c,
              dependency_constraints d)
          : version (move (v)),
            committed (c),
            dependencies (move (d)) {}
    };
  }
}

#endif // BUILD2_VERSION_MODULE_HXX
