// file      : build2/cc/common.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2016 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <build2/cc/common>

#include <build2/file>   // import()
#include <build2/scope>
#include <build2/context>
#include <build2/variable>
#include <build2/algorithm>
#include <build2/filesystem>
#include <build2/diagnostics>

#include <build2/cc/utility>

using namespace std;
using namespace butl;

namespace build2
{
  namespace cc
  {
    using namespace bin;

    // Recursively process prerequisite libraries. If proc_impl returns false,
    // then only process interface (*.export.libs), otherwise -- interface and
    // implementation (prerequisite and from *.libs, unless overriden).
    //
    // Note that here we assume that an interface library is also an
    // implementation (since we don't use *.export.libs in static link). We
    // currently have this restriction to make sure the target in
    // *.export.libs is up-to-date (which will happen automatically if it is
    // listed as a prerequisite of this library).
    //
    // Storing a reference to library path in proc_lib is legal (it comes
    // either from the target's path or from one of the *.libs variables
    // neither of which should change on this run).
    //
    void common::
    process_libraries (
      scope& top_bs,
      lorder top_lo,
      const dir_paths& top_sysd,
      file& l,
      bool la,
      const function<bool (file&,
                           bool la)>& proc_impl, // Implementation?
      const function<void (file*,                // Can be NULL.
                           const string& path,   // Library path.
                           bool sys)>& proc_lib, // True if system library.
      const function<void (file&,
                           const string& type,   // cc.type
                           bool com,             // cc. or x.
                           bool exp)>& proc_opt, // *.export.
      bool self /*= false*/) const               // Call proc_lib on l?
    {
      // Determine if an absolute path is to a system library. Note that
      // we assume both paths to be normalized.
      //
      auto sys = [] (const dir_paths& sysd, const string& p) -> bool
      {
        size_t pn (p.size ());

        for (const dir_path& d: sysd)
        {
          const string& ds (d.string ()); // Can be "/", otherwise no slash.
          size_t dn (ds.size ());

          if (pn > dn &&
              p.compare (0, dn, ds) == 0 &&
              (path::traits::is_separator (ds[dn - 1]) ||
               path::traits::is_separator (p[dn])))
            return true;
        }

        return false;
      };

      // See what type of library this is (C, C++, etc). Use it do decide
      // which x.libs variable name to use. If it's unknown, then we only
      // look into prerequisites.
      //
      const string* t (cast_null<string> (l.vars[c_type]));

      if (self && proc_lib)
      {
        const string& p (l.path ().string ());

        bool s (t != nullptr // If cc library (matched or imported).
                ? cast_false<bool> (l.vars[c_system])
                : sys (top_sysd, p));

        proc_lib (&l, p, s);
      }

      bool impl (proc_impl && proc_impl (l, la));
      bool cc, same;

      auto& vp (var_pool);
      lookup c_e_libs;
      lookup x_e_libs;

      if (t != nullptr)
      {
        cc = *t == "cc";
        same = !cc && *t == x;

        // The explicit export override should be set on the liba/libs{}
        // target itself. Note also that we only check for *.libs. If one
        // doesn't have any libraries but needs to set, say, *.loptions, then
        // *.libs should be set to NULL or empty (this is why we check for
        // the result being defined).
        //
        if (impl)
          c_e_libs = l.vars[c_export_libs]; // Override.
        else if (l.group != nullptr) // lib{} group.
          c_e_libs = l.group->vars[c_export_libs];

        if (!cc)
        {
          const variable& var (same
                               ? x_export_libs
                               : vp[*t + ".export.libs"]);

          if (impl)
            x_e_libs = l.vars[var]; // Override.
          else if (l.group != nullptr) // lib{} group.
            x_e_libs = l.group->vars[var];
        }
      }

      scope& bs (t == nullptr || cc ? top_bs : l.base_scope ());
      optional<lorder> lo;                       // Calculate lazily.
      const dir_paths* sysd (nullptr);           // Resolve lazily.

      // Find system search directories corresponding to this library, i.e.,
      // from its project and for its type (C, C++, etc).
      //
      auto find_sysd = [&top_sysd, t, cc, same, &bs, &sysd, this] ()
      {
        // Use the search dirs corresponding to this library scope/type.
        //
        sysd = (t == nullptr || cc)
        ? &top_sysd // Imported library, use importer's sysd.
        : &cast<dir_paths> (
          bs.root_scope ()->vars[same
                                 ? x_sys_lib_dirs
                                 : var_pool[*t + ".sys_lib_dirs"]]);
      };

      auto find_lo = [top_lo, t, cc, &bs, &l, &lo, this] ()
      {
        lo = (t == nullptr || cc) ? top_lo : link_order (bs, link_type (l));
      };

      // Only go into prerequisites (implementation) if instructed and we are
      // not using explicit export. Otherwise, interface dependencies come
      // from the lib{}:*.export.libs below.
      //
      if (impl && !c_e_libs.defined () && !x_e_libs.defined ())
      {
        for (target* p: l.prerequisite_targets)
        {
          bool a;
          file* f;

          if ((a = (f = p->is_a<liba> ()) != nullptr)
              ||   (f = p->is_a<libs> ()) != nullptr)
          {
            if (sysd == nullptr) find_sysd ();
            if (!lo) find_lo ();

            process_libraries (bs, *lo, *sysd,
                               *f, a,
                               proc_impl, proc_lib, proc_opt, true);
          }
        }
      }

      // Process libraries (recursively) from *.export.libs (of type names)
      // handling import, etc.
      //
      // If it is not a C-common library, then it probably doesn't have any of
      // the *.libs and we are done.
      //
      if (t == nullptr)
        return;

      optional<dir_paths> usrd;                  // Extract lazily.

      // Determine if a "simple path" is a system library.
      //
      auto sys_simple = [&sysd, &sys, &find_sysd] (const string& p) -> bool
      {
        bool s (!path::traits::absolute (p));

        if (!s)
        {
          if (sysd == nullptr) find_sysd ();

          s = sys (*sysd, p);
        }

        return s;
      };

      auto proc_int = [&l,
                       &proc_impl, &proc_lib, &proc_opt,
                       &sysd, &usrd,
                       &find_sysd, &find_lo, &sys, &sys_simple,
                       &bs, &lo, this] (const lookup& lu)
      {
        const names* ns (cast_null<names> (lu));
        if (ns == nullptr || ns->empty ())
          return;

        for (const name& n: *ns)
        {
          if (n.simple ())
          {
            // This is something like -lpthread or shell32.lib so should be a
            // valid path. But it can also be an absolute library path (e.g.,
            // something that in the future will come from our -static/-shared
            // .pc files.
            //
            if (proc_lib)
              proc_lib (nullptr, n.value, sys_simple (n.value));
          }
          else
          {
            // This is a potentially project-qualified target.
            //
            if (sysd == nullptr) find_sysd ();
            if (!lo) find_lo ();

            file& t (resolve_library (n, bs, *lo, *sysd, usrd));

            if (proc_lib)
            {
              // This can happen if the target is mentioned in *.export.libs
              // (i.e., it is an interface dependency) but not in the
              // library's prerequisites (i.e., it is not an implementation
              // dependency).
              //
              // Note that we used to just check for path being assigned but
              // on Windows import-installed DLLs may legally have empty
              // paths.
              //
              if (t.mtime (false) == timestamp_unknown)
                fail << "interface dependency " << t << " is out of date" <<
                  info << "mentioned in *.export.libs of target " << l <<
                  info << "is it a prerequisite of " << l << "?";
            }

            // Process it recursively.
            //
            process_libraries (bs, *lo, *sysd,
                               t, t.is_a<liba> (),
                               proc_impl, proc_lib, proc_opt, true);
          }
        }
      };

      // Process libraries from *.libs (of type strings).
      //
      auto proc_imp = [&proc_lib, &sys_simple] (const lookup& lu)
      {
        const strings* ns (cast_null<strings> (lu));
        if (ns == nullptr || ns->empty ())
          return;

        for (const string& n: *ns)
        {
          // This is something like -lpthread or shell32.lib so should be a
          // valid path.
          //
          proc_lib (nullptr, n, sys_simple (n));
        }
      };

      // If all we know is it's a C-common library, then in both cases we only
      // look for cc.export.libs.
      //
      if (cc)
      {
        if (proc_opt) proc_opt (l, *t, true, true);
        if (c_e_libs) proc_int (c_e_libs);
      }
      else
      {
        if (impl)
        {
          // Interface and implementation: as discussed above, we can have two
          // situations: overriden export or default export.
          //
          if (c_e_libs.defined () || x_e_libs.defined ())
          {
            // NOTE: should this not be from l.vars rather than l? Or perhaps
            // we can assume non-common values will be set on libs{}/liba{}.
            //
            if (proc_opt) proc_opt (l, *t, true, true);
            if (c_e_libs) proc_int (c_e_libs);

            if (proc_opt) proc_opt (l, *t, false, true);
            if (x_e_libs) proc_int (x_e_libs);
          }
          else
          {
            // For default export we use the same options/libs as were used to
            // build the library. Since libraries in (non-export) *.libs are
            // not targets, we don't need to recurse.
            //
            if (proc_opt) proc_opt (l, *t, true, false);
            if (proc_lib) proc_imp (l[c_libs]);

            if (proc_opt) proc_opt (l, *t, false, false);
            if (proc_lib) proc_imp (l[same ? x_libs : vp[*t + ".libs"]]);
          }
        }
        else
        {
          // Interface: only add *.export.* (interface dependencies).
          //
          if (proc_opt) proc_opt (l, *t, true, true);
          if (c_e_libs) proc_int (c_e_libs);

          if (proc_opt) proc_opt (l, *t, false, true);
          if (x_e_libs) proc_int (x_e_libs);
        }
      }
    }

    // The name can be an absolute target name (e.g., /tmp/libfoo/lib{foo}) or
    // a potentially project-qualified relative target name (e.g.,
    // libfoo%lib{foo}).
    //
    // Note that the scope, search paths, and the link order should all be
    // derived from the library target that mentioned this name. This way we
    // will select exactly the same target as the library's matched rule and
    // that's the only way to guarantee it will be up-to-date.
    //
    file& common::
    resolve_library (name n,
                     scope& s,
                     lorder lo,
                     const dir_paths& sysd,
                     optional<dir_paths>& usrd) const
    {
      if (n.type != "lib" && n.type != "liba" && n.type != "libs")
        fail << "target name " << n << " is not a library";

      target* xt (nullptr);

      if (n.dir.absolute () && !n.qualified ())
      {
        // Search for an existing target with this name "as if" it was a
        // prerequisite.
        //
        xt = &search (move (n), s);
      }
      else
      {
        // This is import.
        //
        const string* ext;
        const target_type* tt (s.find_target_type (n, ext)); // Changes name.

        if (tt == nullptr)
          fail << "unknown target type '" << n.type << "' in library " << n;

        // @@ OUT: for now we assume out is undetermined, just like in
        // search (name, scope).
        //
        dir_path out;
        prerequisite_key pk {n.proj, {tt, &n.dir, &out, &n.value, ext}, &s};
        xt = search_library (sysd, usrd, pk);

        if (xt == nullptr)
        {
          if (n.qualified ())
            xt = &import (pk);
          else
            fail << "unable to find library " << pk;
        }
      }

      // If this is lib{}, pick appropriate member.
      //
      if (lib* l = xt->is_a<lib> ())
        xt = &link_member (*l, lo); // Pick liba{} or libs{}.

      return static_cast<file&> (*xt);
    }

    // Note that pk's scope should not be NULL (even if dir is absolute). If
    // sys is not NULL, then store there an inidication of whether this is a
    // system library.
    //
    target* common::
    search_library (const dir_paths& sysd,
                    optional<dir_paths>& usrd,
                    const prerequisite_key& p) const
    {
      tracer trace (x, "search_library");

      assert (p.scope != nullptr);

      // @@ This is hairy enough to warrant a separate implementation for
      //    Windows.
      //
      bool l (p.is_a<lib> ());
      const string* ext (l ? nullptr : p.tk.ext); // Only for liba/libs.

      // Then figure out what we need to search for.
      //
      const string& name (*p.tk.name);

      // liba
      //
      path an;
      const string* ae (nullptr);

      if (l || p.is_a<liba> ())
      {
        // We are trying to find a library in the search paths extracted from
        // the compiler. It would only be natural if we used the library
        // prefix/extension that correspond to this compiler and/or its
        // target.
        //
        // Unlike MinGW, VC's .lib/.dll.lib naming is by no means standard and
        // we might need to search for other names. In fact, there is no
        // reliable way to guess from the file name what kind of library it
        // is, static or import and we will have to do deep inspection of such
        // alternative names. However, if we did find .dll.lib, then we can
        // assume that .lib is the static library without any deep inspection
        // overhead.
        //
        const char* e ("");

        if (cid == "msvc")
        {
          an = path (name);
          e = "lib";
        }
        else
        {
          an = path ("lib" + name);
          e = "a";
        }

        ae = ext == nullptr
          ? &extension_pool.find (e)
          : ext;

        if (!ae->empty ())
        {
          an += '.';
          an += *ae;
        }
      }

      // libs
      //
      path sn;
      const string* se (nullptr);

      if (l || p.is_a<libs> ())
      {
        const char* e ("");

        if (cid == "msvc")
        {
          sn = path (name);
          e = "dll.lib";
        }
        else
        {
          sn = path ("lib" + name);

          if      (tsys == "darwin")  e = "dylib";
          else if (tsys == "mingw32") e = "dll.a"; // See search code below.
          else                        e = "so";
        }

        se = ext == nullptr
          ? &extension_pool.find (e)
          : ext;

        if (!se->empty ())
        {
          sn += '.';
          sn += *se;
        }
      }

      // Now search.
      //
      liba* a (nullptr);
      libs* s (nullptr);

      path f; // Reuse the buffer.
      const dir_path* pd (nullptr);

      auto search =[&a, &s,
                    &an, &ae,
                    &sn, &se,
                    &name, ext,
                    &p, &f, &trace, this] (const dir_path& d) -> bool
      {
        timestamp mt;

        // libs
        //
        // Look for the shared library first. The order is important for VC:
        // only if we found .dll.lib can we safely assumy that just .lib is a
        // static library.
        //
        if (!sn.empty ())
        {
          f = d;
          f /= sn;
          mt = file_mtime (f);

          if (mt != timestamp_nonexistent)
          {
            // On Windows what we found is the import library which we need
            // to make the first ad hoc member of libs{}.
            //
            if (tclass == "windows")
            {
              s = &targets.insert<libs> (d, dir_path (), name, nullptr, trace);

              if (s->member == nullptr)
              {
                libi& i (
                  targets.insert<libi> (d, dir_path (), name, se, trace));

                if (i.path ().empty ())
                  i.path (move (f));

                i.mtime (mt);

                // Presumably there is a DLL somewhere, we just don't know
                // where (and its possible we might have to look for one if we
                // decide we need to do rpath emulation for installed
                // libraries as well). We will represent this as empty path
                // but valid timestamp (aka "trust me, it's there").
                //
                s->mtime (mt);
                s->member = &i;
              }
            }
            else
            {
              s = &targets.insert<libs> (d, dir_path (), name, se, trace);

              if (s->path ().empty ())
                s->path (move (f));

              s->mtime (mt);
            }
          }
          else if (ext == nullptr && tsys == "mingw32")
          {
            // Above we searched for the import library (.dll.a) but if it's
            // not found, then we also search for the .dll (unless the
            // extension was specified explicitly) since we can link to it
            // directly. Note also that the resulting libs{} would end up
            // being the .dll.
            //
            se = &extension_pool.find ("dll");
            f = f.base (); // Remove .a from .dll.a.
            mt = file_mtime (f);

            if (mt != timestamp_nonexistent)
            {
              s = &targets.insert<libs> (d, dir_path (), name, se, trace);

              if (s->path ().empty ())
                s->path (move (f));

              s->mtime (mt);
            }
          }
        }

        // liba
        //
        // If we didn't find .dll.lib then we cannot assume .lib is static.
        //
        if (!an.empty () && (s != nullptr || cid != "msvc"))
        {
          f = d;
          f /= an;

          if ((mt = file_mtime (f)) != timestamp_nonexistent)
          {
            // Enter the target. Note that because the search paths are
            // normalized, the result is automatically normalized as well.
            //
            // Note that this target is outside any project which we treat
            // as out trees.
            //
            a = &targets.insert<liba> (d, dir_path (), name, ae, trace);

            if (a->path ().empty ())
              a->path (move (f));

            a->mtime (mt);
          }
        }

        // Alternative search for VC.
        //
        if (cid == "msvc")
        {
          scope& rs (*p.scope->root_scope ());
          const process_path& ld (cast<process_path> (rs["bin.ld.path"]));

          if (s == nullptr && !sn.empty ())
            s = msvc_search_shared (ld, d, p);

          if (a == nullptr && !an.empty ())
            a = msvc_search_static (ld, d, p);
        }

        return a != nullptr || s != nullptr;
      };

      // First try user directories (i.e., -L).
      //
      bool sys (false);

      if (!usrd)
        usrd = extract_library_dirs (*p.scope);

      for (const dir_path& d: *usrd)
      {
        if (search (d))
        {
          pd = &d;
          break;
        }
      }

      // Next try system directories (i.e., those extracted from the compiler).
      //
      if (pd == nullptr)
      {
        for (const dir_path& d: sysd)
        {
          if (search (d))
          {
            pd = &d;
            break;
          }
        }

        sys = true;
      }

      if (pd == nullptr)
        return nullptr;

      // Add the "using static/shared library" macro (used, for example, to
      // handle DLL export). The absence of either of these macros would mean
      // some other build system that cannot distinguish between the two (and
      // no pkg-config information).
      //
      auto add_macro = [this] (target& t, const char* suffix)
      {
        // If there is already a value (either in cc.export or x.export),
        // don't add anything: we don't want to be accumulating defines nor
        // messing with custom values. And if we are adding, then use the
        // generic cc.export.
        //
        // The only way we could already have this value is if this same
        // library was also imported as a project (as opposed to installed).
        // Unlikely but possible. In this case the values were set by the
        // export stub and we shouldn't touch them.
        //
        if (!t.vars[x_export_poptions])
        {
          auto p (t.vars.insert (c_export_poptions));

          if (p.second)
          {
            // The "standard" macro name will be LIB<NAME>_{STATIC,SHARED},
            // where <name> is the target name. Here we want to strike a
            // balance between being unique and not too noisy.
            //
            string d ("-DLIB");

            auto upcase_sanitize = [] (char c)
            {
              return (c == '-' || c == '+' || c == '.') ? '_' : ucase (c);
            };

            transform (t.name.begin (),
                       t.name.end (),
                       back_inserter (d),
                       upcase_sanitize);

            d += '_';
            d += suffix;

            strings o;
            o.push_back (move (d));
            p.first.get () = move (o);
          }
        }
      };

      // Enter (or find) the lib{} target group. Note that we must be careful
      // here since its possible we have already imported some of its members.
      //
      lib& lt (
        targets.insert<lib> (
          *pd, dir_path (), name, l ? p.tk.ext : nullptr, trace));

      // It should automatically link-up to the members we have found.
      //
      assert (a == nullptr || lt.a == a);
      assert (s == nullptr || lt.s == s);

      // Update the bin.lib variable to indicate what's available.
      //
      const char* bl (lt.a != nullptr
                      ? (lt.s != nullptr ? "both" : "static")
                      : "shared");
      lt.assign ("bin.lib") = bl;

      target* r (l ? &lt : (p.is_a<liba> () ? static_cast<target*> (a) : s));

      // Mark as a "cc" library (unless already marked) and set the system
      // flag.
      //
      auto mark_cc = [sys, this] (target& t) -> bool
      {
        auto p (t.vars.insert (c_type));

        if (p.second)
        {
          p.first.get () = string ("cc");

          if (sys)
            t.vars.assign (c_system) = true;
        }

        return p.second;
      };

      // If the library already has cc.type, then assume it was either already
      // imported or was matched by a rule.
      //
      if (a != nullptr && !mark_cc (*a))
        a = nullptr;

      if (s != nullptr && !mark_cc (*s))
        s = nullptr;

      if (a != nullptr || s != nullptr)
      {
        // Try to extract library information from pkg-config. We only add the
        // default macro if we could not extract more precise information. The
        // idea is that when we auto-generate .pc files, we will copy those
        // macros (or custom ones) from *.export.poptions.
        //
        if (pkgconfig == nullptr ||
            !pkgconfig_extract (*p.scope, lt, a, s, p.proj, name, *pd, sysd))
        {
          if (a != nullptr) add_macro (*a, "STATIC");
          if (s != nullptr) add_macro (*s, "SHARED");
        }
      }

      return r;
    }

    dir_paths common::
    extract_library_dirs (scope& bs) const
    {
      dir_paths r;

      // Extract user-supplied search paths (i.e., -L, /LIBPATH).
      //
      auto extract = [&r, this] (const value& val)
      {
        const auto& v (cast<strings> (val));

        for (auto i (v.begin ()), e (v.end ()); i != e; ++i)
        {
          const string& o (*i);

          dir_path d;

          if (cid == "msvc")
          {
            // /LIBPATH:<dir> (case-insensitive).
            //
            if ((o[0] == '/' || o[0] == '-') &&
                casecmp (i->c_str () + 1, "LIBPATH:", 8) == 0)
              d = dir_path (*i, 9, string::npos);
            else
              continue;
          }
          else
          {
            // -L can either be in the "-L<dir>" or "-L <dir>" form.
            //
            if (*i == "-L")
            {
              if (++i == e)
                break; // Let the compiler complain.

              d = dir_path (*i);
            }
            else if (i->compare (0, 2, "-L") == 0)
              d = dir_path (*i, 2, string::npos);
            else
              continue;
          }

          // Ignore relative paths. Or maybe we should warn?
          //
          if (!d.relative ())
            r.push_back (move (d));
        }
      };

      if (auto l = bs[c_loptions]) extract (*l);
      if (auto l = bs[x_loptions]) extract (*l);

      return r;
    }
  }
}