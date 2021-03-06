// file      : build2/cc/types.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BUILD2_CC_TYPES_HXX
#define BUILD2_CC_TYPES_HXX

#include <build2/types.hxx>
#include <build2/utility.hxx>

#include <build2/target-type.hxx>

namespace build2
{
  namespace cc
  {
    // Translation unit information.
    //
    struct module_import
    {
      string name;
      bool   exported; // True if re-exported (export import M;).
      size_t score;    // See compile::search_modules().
    };

    using module_imports = vector<module_import>;

    struct module_info
    {
      string         name;          // Not empty if a module unit.
      bool           iface = false; // True if a module interface unit.
      module_imports imports;       // Imported modules.
    };

    enum class translation_type {plain, module_iface, module_impl};

    struct translation_unit
    {
      module_info mod;

      translation_type
      type () const
      {
        return (mod.name.empty () ? translation_type::plain :
                mod.iface         ? translation_type::module_iface
                :                   translation_type::module_impl);
      }
    };

    // Compiler language.
    //
    enum class lang {c, cxx};

    inline ostream&
    operator<< (ostream& os, lang l)
    {
      return os << (l == lang::c ? "C" : "C++");
    }

    // Compile/link output type (executable, static, or shared).
    //
    enum class otype {e, a, s};

    struct ltype
    {
      otype type;
      bool  utility; // True for utility libraries.

      bool executable ()     const {return type == otype::e && !utility;}
      bool library ()        const {return type != otype::e ||  utility;}
      bool static_library () const {return type == otype::a ||  utility;}
      bool shared_library () const {return type == otype::s && !utility;}
    };

    // Compile target types.
    //
    struct compile_target_types
    {
      const target_type& obj;
      const target_type& bmi;
    };

    // Library link order.
    //
    enum class lorder {a, s, a_s, s_a};

    // Link information: output type and link order.
    //
    struct linfo
    {
      otype  type;
      lorder order;
    };

    // Prerequisite link flags.
    //
    using lflags = uintptr_t; // To match prerequisite_target::data.

    const lflags lflag_whole = 0x00000001U; // Link whole liba{}/libu*}.
  }
}

#endif // BUILD2_CC_TYPES_HXX
