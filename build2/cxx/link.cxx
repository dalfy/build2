// file      : build2/cxx/link.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2016 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <build2/cxx/link>

#include <cstdlib> // exit()

#include <butl/path-map>

#include <build2/depdb>
#include <build2/scope>
#include <build2/context>
#include <build2/variable>
#include <build2/algorithm>
#include <build2/filesystem>
#include <build2/diagnostics>

#include <build2/bin/target>
#include <build2/cxx/target>

#include <build2/cxx/common>
#include <build2/cxx/utility>

using namespace std;
using namespace butl;

namespace build2
{
  namespace cxx
  {
    using namespace bin;

    // Extract system library search paths from GCC or compatible (Clang,
    // Intel C++) using the -print-search-dirs option.
    //
    static void
    gcc_library_search_paths (scope& bs, const string& cid, dir_paths& r)
    {
      scope& rs (*bs.root_scope ());

      cstrings args;
      string std_storage;

      args.push_back (cast<path> (rs["config.cxx"]).string ().c_str ());
      append_options (args, bs, "cxx.coptions");
      append_std (args, rs, cid, bs, std_storage);
      append_options (args, bs, "cxx.loptions");
      args.push_back ("-print-search-dirs");
      args.push_back (nullptr);

      if (verb >= 3)
        print_process (args);

      string l;
      try
      {
        process pr (args.data (), 0, -1); // Open pipe to stdout.

        try
        {
          ifdstream is (pr.in_ofd, fdstream_mode::skip, ifdstream::badbit);

          string s;
          while (getline (is, s))
          {
            if (s.compare (0, 12, "libraries: =") == 0)
            {
              l.assign (s, 12, string::npos);
              break;
            }
          }

          is.close (); // Don't block.

          if (!pr.wait ())
            throw failed ();
        }
        catch (const ifdstream::failure&)
        {
          pr.wait ();
          fail << "error reading C++ compiler -print-search-dirs output";
        }
      }
      catch (const process_error& e)
      {
        error << "unable to execute " << args[0] << ": " << e.what ();

        if (e.child ())
          exit (1);

        throw failed ();
      }

      if (l.empty ())
        fail << "unable to extract C++ compiler system library paths";

      // Now the fun part: figuring out which delimiter is used. Normally it
      // is ':' but on Windows it is ';' (or can be; who knows for sure). Also
      // note that these paths are absolute (or should be). So here is what we
      // are going to do: first look for ';'. If found, then that's the
      // delimiter. If not found, then there are two cases: it is either a
      // single Windows path or the delimiter is ':'. To distinguish these two
      // cases we check if the path starts with a Windows drive.
      //
      char d (';');
      string::size_type e (l.find (d));

      if (e == string::npos &&
          (l.size () < 2 || l[0] == '/' || l[1] != ':'))
      {
        d = ':';
        e = l.find (d);
      }

      // Now chop it up. We already have the position of the first delimiter
      // (if any).
      //
      for (string::size_type b (0);; e = l.find (d, (b = e + 1)))
      {
        r.emplace_back (l, b, (e != string::npos ? e - b : e));
        r.back ().normalize ();

        if (e == string::npos)
          break;
      }
    }

    // Extract system library search paths from MSVC.
    //
    void
    msvc_library_search_paths (scope&, const string&, dir_paths&); // msvc.cxx

    dir_paths link::
    extract_library_paths (scope& bs)
    {
      dir_paths r;
      scope& rs (*bs.root_scope ());
      const string& cid (cast<string> (rs["cxx.id"]));

      // Extract user-supplied search paths (i.e., -L, /LIBPATH).
      //
      if (auto l = bs["cxx.loptions"])
      {
        const auto& v (cast<strings> (l));

        for (auto i (v.begin ()), e (v.end ()); i != e; ++i)
        {
          const string& o (*i);

          dir_path d;

          if (cid == "msvc")
          {
            // /LIBPATH:<dir> (case-insensitive).
            //
            if ((o[0] == '/' || o[0] == '-') &&
                (i->compare (1, 8, "LIBPATH:") == 0 ||
                 i->compare (1, 8, "libpath:") == 0))
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
      }

      if (cid == "msvc")
        msvc_library_search_paths (bs, cid, r);
      else
        gcc_library_search_paths (bs, cid, r);

      return r;
    }

    // Alternative search for VC (msvc.cxx).
    //
    liba*
    msvc_search_static (const path& ld, const dir_path&, prerequisite&);

    libs*
    msvc_search_shared (const path& ld, const dir_path&, prerequisite&);

    target* link::
    search_library (optional<dir_paths>& spc, prerequisite& p)
    {
      tracer trace ("cxx::link::search_library");

      // @@ This is hairy enough to warrant a separate implementation for
      //    Windows.
      //

      // First check the cache.
      //
      if (p.target != nullptr)
        return p.target;

      scope& rs (*p.scope.root_scope ());
      const string& cid (cast<string> (rs["cxx.id"]));
      const string& tsys (cast<string> (rs["cxx.target.system"]));
      const string& tclass (cast<string> (rs["cxx.target.class"]));

      bool l (p.is_a<lib> ());
      const string* ext (l ? nullptr : p.ext); // Only for liba/libs.

      // Then figure out what we need to search for.
      //

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
          an = path (p.name);
          e = "lib";
        }
        else
        {
          an = path ("lib" + p.name);
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
          sn = path (p.name);
          e = "dll.lib";
        }
        else
        {
          sn = path ("lib" + p.name);

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
      if (!spc)
        spc = extract_library_paths (p.scope);

      liba* a (nullptr);
      libs* s (nullptr);

      path f; // Reuse the buffer.
      const dir_path* pd;
      for (const dir_path& d: *spc)
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
              s = &targets.insert<libs> (
                d, dir_path (), p.name, nullptr, trace);

              if (s->member == nullptr)
              {
                libi& i (
                  targets.insert<libi> (
                    d, dir_path (), p.name, se, trace));

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
              s = &targets.insert<libs> (d, dir_path (), p.name, se, trace);

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
              s = &targets.insert<libs> (d, dir_path (), p.name, se, trace);

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
            a = &targets.insert<liba> (d, dir_path (), p.name, ae, trace);

            if (a->path ().empty ())
              a->path (move (f));

            a->mtime (mt);
          }
        }

        // Alternative search for VC.
        //
        if (cid == "msvc")
        {
          const path& ld (cast<path> (rs["config.bin.ld"]));

          if (s == nullptr && !sn.empty ())
            s = msvc_search_shared (ld, d, p);

          if (a == nullptr && !an.empty ())
            a = msvc_search_static (ld, d, p);
        }

        if (a != nullptr || s != nullptr)
        {
          pd = &d;
          break;
        }
      }

      if (a == nullptr && s == nullptr)
        return nullptr;

      // Add the "using static/shared library" macro (used, for example, to
      // handle DLL export). The absence of either of these macros would mean
      // some other build system that cannot distinguish between the two.
      //
      auto add_macro = [] (target& t, const char* suffix)
      {
        // If there is already a value, don't add anything, we don't want to
        // be accumulating defines nor messing with custom values.
        //
        auto p (t.vars.insert ("cxx.export.poptions"));

        if (p.second)
        {
          // The "standard" macro name will be LIB<NAME>_{STATIC,SHARED},
          // where <name> is the target name. Here we want to strike a balance
          // between being unique and not too noisy.
          //
          string d ("-DLIB");

          auto upcase_sanitize = [] (char c) -> char
          {
            if (c >= 'a' && c <='z')
            {
              const unsigned char shift ('a' - 'A');
              return c - shift;
            }
            else if (c == '-' || c == '+' || c == '.')
              return '_';
            else
              return c;
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
      };

      if (a != nullptr)
        add_macro (*a, "STATIC");

      if (s != nullptr)
        add_macro (*s, "SHARED");

      if (l)
      {
        // Enter the target group.
        //
        lib& l (targets.insert<lib> (*pd, dir_path (), p.name, p.ext, trace));

        // It should automatically link-up to the members we have found.
        //
        assert (l.a == a);
        assert (l.s == s);

        // Set the bin.lib variable to indicate what's available.
        //
        const char* bl (a != nullptr
                        ? (s != nullptr ? "both" : "static")
                        : "shared");
        l.assign ("bin.lib") = bl;

        p.target = &l;
      }
      else
        p.target = p.is_a<liba> () ? static_cast<target*> (a) : s;

      return p.target;
    }

    match_result link::
    match (action a, target& t, const string& hint) const
    {
      tracer trace ("cxx::link::match");

      // @@ TODO:
      //
      // - if path already assigned, verify extension?
      //
      // @@ Q:
      //
      // - if there is no .o, are we going to check if the one derived
      //   from target exist or can be built? A: No.
      //   What if there is a library. Probably ok if static, not if shared,
      //   (i.e., a utility library).
      //

      otype lt (link_type (t));

      // Scan prerequisites and see if we can work with what we've got.
      //
      bool seen_cxx (false), seen_c (false), seen_obj (false),
        seen_lib (false);

      for (prerequisite_member p: group_prerequisite_members (a, t))
      {
        if (p.is_a<cxx> ())
        {
          seen_cxx = seen_cxx || true;
        }
        else if (p.is_a<c> ())
        {
          seen_c = seen_c || true;
        }
        else if (p.is_a<obj> ())
        {
          seen_obj = seen_obj || true;
        }
        else if (p.is_a<obje> ())
        {
          if (lt != otype::e)
            fail << "obje{} as prerequisite of " << t;

          seen_obj = seen_obj || true;
        }
        else if (p.is_a<obja> ())
        {
          if (lt != otype::a)
            fail << "obja{} as prerequisite of " << t;

          seen_obj = seen_obj || true;
        }
        else if (p.is_a<objs> ())
        {
          if (lt != otype::s)
            fail << "objs{} as prerequisite of " << t;

          seen_obj = seen_obj || true;
        }
        else if (p.is_a<lib> ()  ||
                 p.is_a<liba> () ||
                 p.is_a<libs> ())
        {
          seen_lib = seen_lib || true;
        }
      }

      // We will only chain a C source if there is also a C++ source or we
      // were explicitly told to.
      //
      if (seen_c && !seen_cxx && hint < "cxx")
      {
        l4 ([&]{trace << "C prerequisite(s) without C++ or hint";});
        return nullptr;
      }

      // If we have any prerequisite libraries (which also means that
      // we match), search/import and pre-match them to implement the
      // "library meta-information protocol". Don't do this if we are
      // called from the install rule just to check if we would match.
      //
      if (seen_lib && lt != otype::e &&
          a.operation () != install_id && a.outer_operation () != install_id)
      {
        if (t.group != nullptr)
          t.group->prerequisite_targets.clear (); // lib{}'s

        optional<dir_paths> lib_paths; // Extract lazily.

        for (prerequisite_member p: group_prerequisite_members (a, t))
        {
          if (p.is_a<lib> () || p.is_a<liba> () || p.is_a<libs> ())
          {
            target* pt (nullptr);

            // Handle imported libraries.
            //
            if (p.proj () != nullptr)
              pt = search_library (lib_paths, p.prerequisite);

            if (pt == nullptr)
            {
              pt = &p.search ();
              match_only (a, *pt);
            }

            // If the prerequisite came from the lib{} group, then also
            // add it to lib's prerequisite_targets.
            //
            if (!p.prerequisite.belongs (t))
              t.group->prerequisite_targets.push_back (pt);

            t.prerequisite_targets.push_back (pt);
          }
        }
      }

      return seen_cxx || seen_c || seen_obj || seen_lib ? &t : nullptr;
    }

    recipe link::
    apply (action a, target& xt, const match_result&) const
    {
      tracer trace ("cxx::link::apply");

      file& t (static_cast<file&> (xt));

      scope& bs (t.base_scope ());
      scope& rs (*bs.root_scope ());

      const string& cid (cast<string> (rs["cxx.id"]));
      const string& tsys (cast<string> (rs["cxx.target.system"]));
      const string& tclass (cast<string> (rs["cxx.target.class"]));

      otype lt (link_type (t));
      lorder lo (link_order (bs, lt));

      // Derive file name from target name.
      //
      if (t.path ().empty ())
      {
        const char* p (nullptr);
        const char* e (nullptr);

        switch (lt)
        {
        case otype::e:
          {
            if (tclass == "windows")
              e = "exe";
            else
              e = "";

            break;
          }
        case otype::a:
          {
            // To be anally precise, let's use the ar id to decide how to name
            // the library in case, for example, someone wants to archive
            // VC-compiled object files with MinGW ar or vice versa.
            //
            if (cast<string> (rs["bin.ar.id"]) == "msvc")
            {
              e = "lib";
            }
            else
            {
              p = "lib";
              e = "a";
            }

            if (auto l = t["bin.libprefix"])
              p = cast<string> (l).c_str ();

            break;
          }
        case otype::s:
          {
            if (tclass == "macosx")
            {
              p = "lib";
              e = "dylib";
            }
            else if (tclass == "windows")
            {
              // On Windows libs{} is an ad hoc group. The libs{} itself is
              // the DLL and we add libi{} import library as its member (see
              // below).
              //
              if (tsys == "mingw32")
                p = "lib";

              e = "dll";
            }
            else
            {
              p = "lib";
              e = "so";
            }

            if (auto l = t["bin.libprefix"])
              p = cast<string> (l).c_str ();

            break;
          }
        }

        t.derive_path (e, p);
      }

      // Add ad hoc group members.
      //
      auto add_adhoc = [a, &bs] (target& t, const char* type) -> file&
      {
        const target_type& tt (*bs.find_target_type (type));

        if (t.member != nullptr) // Might already be there.
          assert (t.member->type () == tt);
        else
          t.member = &search (tt, t.dir, t.out, t.name, nullptr, nullptr);

        file& r (static_cast<file&> (*t.member));
        r.recipe (a, group_recipe);
        return r;
      };

      if (tclass == "windows")
      {
        // Import library.
        //
        if (lt == otype::s)
        {
          file& imp (add_adhoc (t, "libi"));

          // Usually on Windows the import library is called the same as the
          // DLL but with the .lib extension. Which means it clashes with the
          // static library. Instead of decorating the static library name
          // with ugly suffixes (as is customary), let's use the MinGW
          // approach (one must admit it's quite elegant) and call it
          // .dll.lib.
          //
          if (imp.path ().empty ())
            imp.derive_path (t.path (), tsys == "mingw32" ? "a" : "lib");
        }

        // PDB
        //
        if (lt != otype::a &&
            cid == "msvc" &&
            find_option ("/DEBUG", t, "cxx.loptions", true))
        {
          // Add after the import library if any.
          //
          file& pdb (add_adhoc (t.member == nullptr ? t : *t.member, "pdb"));

          // We call it foo.{exe,dll}.pdb rather than just foo.pdb because we
          // can have both foo.exe and foo.dll in the same directory.
          //
          if (pdb.path ().empty ())
            pdb.derive_path (t.path (), "pdb");
        }
      }

      t.prerequisite_targets.clear (); // See lib pre-match in match() above.

      // Inject dependency on the output directory.
      //
      inject_fsdir (a, t);

      optional<dir_paths> lib_paths; // Extract lazily.

      // Process prerequisites: do rule chaining for C and C++ source
      // files as well as search and match.
      //
      // When cleaning, ignore prerequisites that are not in the same
      // or a subdirectory of our project root.
      //
      const target_type& ott (lt == otype::e ? obje::static_type :
                              lt == otype::a ? obja::static_type :
                              objs::static_type);

      for (prerequisite_member p: group_prerequisite_members (a, t))
      {
        target* pt (nullptr);

        if (!p.is_a<c> () && !p.is_a<cxx> ())
        {
          // Handle imported libraries.
          //
          if (p.proj () != nullptr)
            pt = search_library (lib_paths, p.prerequisite);

          // The rest is the same basic logic as in search_and_match().
          //
          if (pt == nullptr)
            pt = &p.search ();

          if (a.operation () == clean_id && !pt->dir.sub (rs.out_path ()))
            continue; // Skip.

          // If this is the obj{} or lib{} target group, then pick the
          // appropriate member and make sure it is searched and matched.
          //
          if (obj* o = pt->is_a<obj> ())
          {
            switch (lt)
            {
            case otype::e: pt = o->e; break;
            case otype::a: pt = o->a; break;
            case otype::s: pt = o->s; break;
            }

            if (pt == nullptr)
              pt = &search (ott, p.key ());
          }
          else if (lib* l = pt->is_a<lib> ())
          {
            pt = &link_member (*l, lo);
          }

          build2::match (a, *pt);
          t.prerequisite_targets.push_back (pt);
          continue;
        }

        // Which scope shall we use to resolve the root? Unlikely, but
        // possible, the prerequisite is from a different project
        // altogether. So we are going to use the target's project.
        //

        // @@ Why are we creating the obj{} group if the source came from a
        //    group?
        //
        bool group (!p.prerequisite.belongs (t)); // Group's prerequisite.

        const prerequisite_key& cp (p.key ()); // c(xx){} prerequisite key.
        const target_type& tt (group ? obj::static_type : ott);

        // Come up with the obj*{} target. The c(xx){} prerequisite directory
        // can be relative (to the scope) or absolute. If it is relative, then
        // use it as is. If absolute, then translate it to the corresponding
        // directory under out_root. While the c(xx){} directory is most
        // likely under src_root, it is also possible it is under out_root
        // (e.g., generated source).
        //
        dir_path d;
        {
          const dir_path& cpd (*cp.tk.dir);

          if (cpd.relative () || cpd.sub (rs.out_path ()))
            d = cpd;
          else
          {
            if (!cpd.sub (rs.src_path ()))
              fail << "out of project prerequisite " << cp <<
                info << "specify corresponding " << tt.name << "{} "
                   << "target explicitly";

            d = rs.out_path () / cpd.leaf (rs.src_path ());
          }
        }

        // obj*{} is always in the out tree.
        //
        target& ot (
          search (tt, d, dir_path (), *cp.tk.name, nullptr, cp.scope));

        // If we are cleaning, check that this target is in the same or
        // a subdirectory of our project root.
        //
        if (a.operation () == clean_id && !ot.dir.sub (rs.out_path ()))
        {
          // If we shouldn't clean obj{}, then it is fair to assume
          // we shouldn't clean cxx{} either (generated source will
          // be in the same directory as obj{} and if not, well, go
          // find yourself another build system ;-)).
          //
          continue; // Skip.
        }

        // If we have created the obj{} target group, pick one of its
        // members; the rest would be primarily concerned with it.
        //
        if (group)
        {
          obj& o (static_cast<obj&> (ot));

          switch (lt)
          {
          case otype::e: pt = o.e; break;
          case otype::a: pt = o.a; break;
          case otype::s: pt = o.s; break;
          }

          if (pt == nullptr)
            pt = &search (ott, o.dir, o.out, o.name, o.ext, nullptr);
        }
        else
          pt = &ot;

        // If this obj*{} target already exists, then it needs to be
        // "compatible" with what we are doing here.
        //
        // This gets a bit tricky. We need to make sure the source files
        // are the same which we can only do by comparing the targets to
        // which they resolve. But we cannot search the ot's prerequisites
        // -- only the rule that matches can. Note, however, that if all
        // this works out, then our next step is to match the obj*{}
        // target. If things don't work out, then we fail, in which case
        // searching and matching speculatively doesn't really hurt.
        //
        bool found (false);
        for (prerequisite_member p1:
               reverse_group_prerequisite_members (a, *pt))
        {
          // Ignore some known target types (fsdir, headers, libraries).
          //
          if (p1.is_a<fsdir> () ||
              p1.is_a<h> ()     ||
              (p.is_a<cxx> () && (p1.is_a<hxx> () ||
                                  p1.is_a<ixx> () ||
                                  p1.is_a<txx> ())) ||
              p1.is_a<lib>  () ||
              p1.is_a<liba> () ||
              p1.is_a<libs> ())
          {
            continue;
          }

          if (!p1.is_a<cxx> ())
            fail << "synthesized target for prerequisite " << cp
                 << " would be incompatible with existing target " << *pt <<
              info << "unexpected existing prerequisite type " << p1 <<
              info << "specify corresponding obj{} target explicitly";

          if (!found)
          {
            build2::match (a, *pt); // Now p1 should be resolved.

            // Searching our own prerequisite is ok.
            //
            if (&p.search () != &p1.search ())
              fail << "synthesized target for prerequisite " << cp << " would "
                   << "be incompatible with existing target " << *pt <<
                info << "existing prerequisite " << p1 << " does not match "
                   << cp <<
                info << "specify corresponding " << tt.name << "{} target "
                   << "explicitly";

            found = true;
            // Check the rest of the prerequisites.
          }
        }

        if (!found)
        {
          // Note: add the source to the group, not the member.
          //
          ot.prerequisites.emplace_back (p.as_prerequisite (trace));

          // Add our lib*{} prerequisites to the object file (see
          // cxx.export.poptions above for details).
          //
          // Note that we don't resolve lib{} to liba{}/libs{} here instead
          // leaving it to whoever (e.g., the compile rule) will be needing
          // cxx.export.*. One reason for doing it there is that the object
          // target might be specified explicitly by the user in which case
          // they will have to specify the set of lib{} prerequisites and it's
          // much cleaner to do as lib{} rather than liba{}/libs{}.
          //
          // Initially, we were only adding imported libraries, but there is a
          // problem with this approach: the non-imported library might depend
          // on the imported one(s) which we will never "see" unless we start
          // with this library.
          //
          for (prerequisite& p: group_prerequisites (t))
          {
            if (p.is_a<lib> () || p.is_a<liba> () || p.is_a<libs> ())
              ot.prerequisites.emplace_back (p);
          }

          build2::match (a, *pt);
        }

        t.prerequisite_targets.push_back (pt);
      }

      switch (a)
      {
      case perform_update_id: return &perform_update;
      case perform_clean_id: return &perform_clean;
      default: return noop_recipe; // Configure update.
      }
    }

    // Recursively append/hash prerequisite libraries of a static library.
    //
    static void
    append_libraries (strings& args, liba& a)
    {
      for (target* pt: a.prerequisite_targets)
      {
        if (liba* pa = pt->is_a<liba> ())
        {
          args.push_back (relative (pa->path ()).string ()); // string()&&
          append_libraries (args, *pa);
        }
        else if (libs* ps = pt->is_a<libs> ())
          args.push_back (relative (ps->path ()).string ()); // string()&&
      }
    }

    static void
    hash_libraries (sha256& cs, liba& a)
    {
      for (target* pt: a.prerequisite_targets)
      {
        if (liba* pa = pt->is_a<liba> ())
        {
          cs.append (pa->path ().string ());
          hash_libraries (cs, *pa);
        }
        else if (libs* ps = pt->is_a<libs> ())
          cs.append (ps->path ().string ());
      }
    }

    static void
    append_rpath_link (strings& args, libs& t)
    {
      for (target* pt: t.prerequisite_targets)
      {
        if (libs* ls = pt->is_a<libs> ())
        {
          args.push_back ("-Wl,-rpath-link," +
                          ls->path ().directory ().string ());
          append_rpath_link (args, *ls);
        }
      }
    }

    // See windows-manifest.cxx.
    //
    path
    windows_manifest (file&, bool rpath_assembly);

    // See windows-rpath.cxx.
    //
    timestamp
    windows_rpath_timestamp (file&);

    void
    windows_rpath_assembly (file&, timestamp, bool scratch);

    const char*
    msvc_machine (const string& cpu); // msvc.cxx

    target_state link::
    perform_update (action a, target& xt)
    {
      tracer trace ("cxx::link::perform_update");

      file& t (static_cast<file&> (xt));

      otype lt (link_type (t));

      // Update prerequisites.
      //
      bool update (execute_prerequisites (a, t, t.mtime ()));

      scope& rs (t.root_scope ());

      const string& cid (cast<string> (rs["cxx.id"]));
      const string& tgt (cast<string> (rs["cxx.target"]));
      const string& tsys (cast<string> (rs["cxx.target.system"]));
      const string& tclass (cast<string> (rs["cxx.target.class"]));

      // If targeting Windows, take care of the manifest.
      //
      path manifest; // Manifest itself (msvc) or compiled object file.
      timestamp rpath_timestamp (timestamp_nonexistent); // DLLs timestamp.

      if (lt == otype::e && tclass == "windows")
      {
        // First determine if we need to add our rpath emulating assembly. The
        // assembly itself is generated later, after updating the target. Omit
        // it if we are updating for install.
        //
        if (a.outer_operation () != install_id)
          rpath_timestamp = windows_rpath_timestamp (t);

        // Whether
        //
        path mf (
          windows_manifest (
            t,
            rpath_timestamp != timestamp_nonexistent));

        if (tsys == "mingw32")
        {
          // Compile the manifest into the object file with windres. While we
          // are going to synthesize an .rc file to pipe to windres' stdin, we
          // will still use .manifest to check if everything is up-to-date.
          //
          manifest = mf + ".o";

          if (file_mtime (mf) > file_mtime (manifest))
          {
            path of (relative (manifest));

            // @@ Would be good to add this to depdb (e.g,, rc changes).
            //
            const char* args[] = {
              cast<path> (rs["config.bin.rc"]).string ().c_str (),
              "--input-format=rc",
              "--output-format=coff",
              "-o", of.string ().c_str (),
              nullptr};

            if (verb >= 3)
              print_process (args);

            try
            {
              process pr (args, -1);

              try
              {
                ofdstream os (pr.out_fd);

                // 1 is resource ID, 24 is RT_MANIFEST. We also need to escape
                // Windows path backslashes.
                //
                os << "1 24 \"";

                const string& s (mf.string ());
                for (size_t i (0), j;; i = j + 1)
                {
                  j = s.find ('\\', i);
                  os.write (s.c_str () + i,
                            (j == string::npos ? s.size () : j) - i);

                  if (j == string::npos)
                    break;

                  os.write ("\\\\", 2);
                }

                os << "\"" << endl;

                os.close ();

                if (!pr.wait ())
                  throw failed (); // Assume diagnostics issued.
              }
              catch (const ofdstream::failure& e)
              {
                if (pr.wait ()) // Ignore if child failed.
                  fail << "unable to pipe resource file to " << args[0]
                       << ": " << e.what ();
              }
            }
            catch (const process_error& e)
            {
              error << "unable to execute " << args[0] << ": " << e.what ();

              if (e.child ())
                exit (1);

              throw failed ();
            }

            update = true; // Force update.
          }
        }
        else
          manifest = move (mf); // Save for link.exe's /MANIFESTINPUT.
      }

      // Check/update the dependency database.
      //
      depdb dd (t.path () + ".d");

      // First should come the rule name/version.
      //
      if (dd.expect ("cxx.link 1") != nullptr)
        l4 ([&]{trace << "rule mismatch forcing update of " << t;});

      lookup ranlib;

      // Then the linker checksum (ar/ranlib or C++ compiler).
      //
      if (lt == otype::a)
      {
        ranlib = rs["config.bin.ranlib"];

        if (ranlib && ranlib->empty ()) // @@ BC LT [null].
          ranlib = lookup ();

        const char* rl (
          ranlib
          ? cast<string> (rs["bin.ranlib.checksum"]).c_str ()
          : "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

        if (dd.expect (cast<string> (rs["bin.ar.checksum"])) != nullptr)
          l4 ([&]{trace << "ar mismatch forcing update of " << t;});

        if (dd.expect (rl) != nullptr)
          l4 ([&]{trace << "ranlib mismatch forcing update of " << t;});
      }
      else
      {
        // For VC we use link.exe directly.
        //
        const string& cs (
          cast<string> (
            rs[cid == "msvc" ? "bin.ld.checksum" : "cxx.checksum"]));

        if (dd.expect (cs) != nullptr)
          l4 ([&]{trace << "linker mismatch forcing update of " << t;});
      }

      // Next check the target. While it might be incorporated into the linker
      // checksum, it also might not (e.g., MS link.exe).
      //
      if (dd.expect (tgt) != nullptr)
        l4 ([&]{trace << "target mismatch forcing update of " << t;});

      // Start building the command line. While we don't yet know whether we
      // will really need it, we need to hash it to find out. So the options
      // are to either replicate the exact process twice, first for hashing
      // then for building or to go ahead and start building and hash the
      // result. The first approach is probably more efficient while the
      // second is simpler. Let's got with the simpler for now (actually it's
      // kind of a hybrid).
      //
      cstrings args {nullptr}; // Reserve one for config.bin.ar/config.cxx.

      // Storage.
      //
      string std;
      string soname1, soname2;
      strings sargs;

      if (lt == otype::a)
      {
        if (cid == "msvc") ;
        else
        {
          // If the user asked for ranlib, don't try to do its function with -s.
          // Some ar implementations (e.g., the LLVM one) doesn't support
          // leading '-'.
          //
          args.push_back (ranlib ? "rc" : "rcs");
        }
      }
      else
      {
        if (cid == "msvc")
        {
          // We are using link.exe directly so we don't pass the C++ compiler
          // options.
        }
        else
        {
          append_options (args, t, "cxx.coptions");
          append_std (args, rs, cid, t, std);
        }

        append_options (args, t, "cxx.loptions");

        // Handle soname/rpath.
        //
        if (tclass == "windows")
        {
          // Limited emulation for Windows with no support for user-defined
          // rpaths.
          //
          auto l (t["bin.rpath"]);

          if (l && !l->empty ())
            fail << tgt << " does not support rpath";
        }
        else
        {
          // Set soname.
          //
          if (lt == otype::s)
          {
            const string& leaf (t.path ().leaf ().string ());

            if (tclass == "macosx")
            {
              // With Mac OS 10.5 (Leopard) Apple finally caved in and gave us
              // a way to emulate vanilla -rpath.
              //
              // It may seem natural to do something different on update for
              // install. However, if we don't make it @rpath, then the user
              // won't be able to use config.bin.rpath for installed libraries.
              //
              soname1 = "-install_name";
              soname2 = "@rpath/" + leaf;
            }
            else
              soname1 = "-Wl,-soname," + leaf;

            if (!soname1.empty ())
              args.push_back (soname1.c_str ());

            if (!soname2.empty ())
              args.push_back (soname2.c_str ());
          }

          // Add rpaths. We used to first add the ones specified by the user
          // so that they take precedence. But that caused problems if we have
          // old versions of the libraries sitting in the rpath location
          // (e.g., installed libraries). And if you think about this, it's
          // probably correct to prefer libraries that we explicitly imported
          // to the ones found via rpath.
          //
          // Note also that if this is update for install, then we don't add
          // rpath of the imported libraries (i.e., we assume they are also
          // installed).
          //
          for (target* pt: t.prerequisite_targets)
          {
            if (libs* ls = pt->is_a<libs> ())
            {
              if (a.outer_operation () != install_id)
              {
                sargs.push_back ("-Wl,-rpath," +
                                 ls->path ().directory ().string ());
              }
              // Use -rpath-link on targets that support it (Linux, FreeBSD).
              // Since with this option the paths are not stored in the
              // library, we have to do this recursively (in fact, we don't
              // really need it for top-level libraries).
              //
              else if (tclass == "linux" || tclass == "freebsd")
                append_rpath_link (sargs, *ls);
            }
          }

          if (auto l = t["bin.rpath"])
            for (const dir_path& p: cast<dir_paths> (l))
              sargs.push_back ("-Wl,-rpath," + p.string ());
        }
      }

      // All the options should now be in. Hash them and compare with the db.
      //
      {
        sha256 cs;

        for (size_t i (1); i != args.size (); ++i)
          cs.append (args[i]);

        for (size_t i (0); i != sargs.size (); ++i)
          cs.append (sargs[i]);

        if (dd.expect (cs.string ()) != nullptr)
          l4 ([&]{trace << "options mismatch forcing update of " << t;});
      }

      // Finally, hash and compare the list of input files.
      //
      // Should we capture actual files or their checksum? The only good
      // reason for capturing actual files is diagnostics: we will be able
      // to pinpoint exactly what is causing the update. On the other hand,
      // the checksum is faster and simpler. And we like simple.
      //
      {
        sha256 cs;

        for (target* pt: t.prerequisite_targets)
        {
          file* f;
          liba* a (nullptr);
          libs* s (nullptr);

          if ((f = pt->is_a<obje> ()) ||
              (f = pt->is_a<obja> ()) ||
              (f = pt->is_a<objs> ()) ||
              (lt != otype::a &&
               ((f = a = pt->is_a<liba> ()) ||
                (f = s = pt->is_a<libs> ()))))
          {
            // On Windows a shared library is a DLL with the import library as
            // a first ad hoc group member. MinGW though can link directly to
            // DLLs (see search_library() for details).
            //
            if (s != nullptr && tclass == "windows")
            {
              if (s->member != nullptr)
                f = static_cast<file*> (s->member);
            }

            cs.append (f->path ().string ());

            // If this is a static library, link all the libraries it depends
            // on, recursively.
            //
            if (a != nullptr)
              hash_libraries (cs, *a);
          }
        }

        // Treat it as input for both MinGW and VC.
        //
        if (!manifest.empty ())
          cs.append (manifest.string ());

        // Treat them as inputs, not options.
        //
        if (lt != otype::a)
          hash_options (cs, t, "cxx.libs");

        if (dd.expect (cs.string ()) != nullptr)
          l4 ([&]{trace << "file set mismatch forcing update of " << t;});
      }

      // If any of the above checks resulted in a mismatch (different linker,
      // options or input file set), or if the database is newer than the
      // target (interrupted update) then force the target update. Also
      // note this situation in the "from scratch" flag.
      //
      bool scratch (false);
      if (dd.writing () || dd.mtime () > t.mtime ())
        scratch = update = true;

      dd.close ();

      // If nothing changed, then we are done.
      //
      if (!update)
        return target_state::unchanged;

      // Ok, so we are updating. Finish building the command line.
      //
      string out, out1, out2; // Storage.

      // Translate paths to relative (to working directory) ones. This results
      // in easier to read diagnostics.
      //
      path relt (relative (t.path ()));

      switch (lt)
      {
      case otype::a:
        {
          args[0] = cast<path> (rs["config.bin.ar"]).string ().c_str ();

          if (cid == "msvc")
          {
            // lib.exe has /LIBPATH but it's not clear/documented what it's
            // used for. Perhaps for link-time code generation (/LTCG)? If
            // that's the case, then we may need to pass cxx.loptions.
            //
            args.push_back ("/NOLOGO");

            // Add /MACHINE.
            //
            args.push_back (
              msvc_machine (cast<string> (rs["cxx.target.cpu"])));

            out = "/OUT:" + relt.string ();
            args.push_back (out.c_str ());
          }
          else
            args.push_back (relt.string ().c_str ());

          break;
        }
        // The options are usually similar enough to handle them together.
        //
      case otype::e:
      case otype::s:
        {
          if (cid == "msvc")
          {
            // Using link.exe directly.
            //
            args[0] = cast<path> (rs["config.bin.ld"]).string ().c_str ();
            args.push_back ("/NOLOGO");

            if (lt == otype::s)
              args.push_back ("/DLL");

            // Add /MACHINE.
            //
            args.push_back (
              msvc_machine (cast<string> (rs["cxx.target.cpu"])));

            // Unless explicitly enabled with /INCREMENTAL, disable
            // incremental linking (it is implicitly enabled if /DEBUG is
            // specified). The reason is the .ilk file: its name cannot be
            // changed and if we have, say, foo.exe and foo.dll, then they
            // will end up stomping on each other's .ilk's.
            //
            // So the idea is to disable it by default but let the user
            // request it explicitly if they are sure their project doesn't
            // suffer from the above issue. We can also have something like
            // 'incremental' config initializer keyword for this.
            //
            // It might also be a good idea to ask Microsoft to add an option.
            //
            if (!find_option ("/INCREMENTAL", args, true))
              args.push_back ("/INCREMENTAL:NO");

            // If you look at the list of libraries Visual Studio links by
            // default, it includes everything and a couple of kitchen sinks
            // (winspool32.lib, ole32.lib, odbc32.lib, etc) while we want to
            // keep our low-level build as pure as possible. However, there
            // seem to be fairly essential libraries that are not linked by
            // link.exe by default (use /VERBOSE:LIB to see the list). For
            // example, MinGW by default links advapi32, shell32, user32, and
            // kernel32. And so we follow suit and make sure those are linked.
            // advapi32 and kernel32 are already on the default list and we
            // only need to add the other two.
            //
            // The way we are going to do it is via the /DEFAULTLIB option
            // rather than specifying the libraries as normal inputs (as VS
            // does). This way the user can override our actions with the
            // /NODEFAULTLIB option.
            //
            args.push_back ("/DEFAULTLIB:shell32.lib");
            args.push_back ("/DEFAULTLIB:user32.lib");

            // Take care of the manifest (will be empty for the DLL).
            //
            if (!manifest.empty ())
            {
              std = "/MANIFESTINPUT:"; // Repurpose storage for std.
              std += relative (manifest).string ();
              args.push_back ("/MANIFEST:EMBED");
              args.push_back (std.c_str ());
            }

            if (lt == otype::s)
            {
              // On Windows libs{} is the DLL and its first ad hoc group
              // member is the import library.
              //
              // This will also create the .exp export file. Its name will be
              // derived from the import library by changing the extension.
              // Lucky for us -- there is no option to name it.
              //
              auto imp (static_cast<file*> (t.member));
              out2 = "/IMPLIB:" + relative (imp->path ()).string ();
              args.push_back (out2.c_str ());
            }

            // If we have /DEBUG then name the .pdb file. It is either the
            // first (exe) or the second (dll) ad hoc group member.
            //
            if (find_option ("/DEBUG", args, true))
            {
              auto pdb (static_cast<file*> (
                          lt == otype::e ? t.member : t.member->member));
              out1 = "/PDB:" + relative (pdb->path ()).string ();
              args.push_back (out1.c_str ());
            }

            // @@ An executable can have an import library and VS seems to
            //    always name it. I wonder what would trigger its generation?
            //    Could it be the presence of export symbols?

            out = "/OUT:" + relt.string ();
            args.push_back (out.c_str ());
          }
          else
          {
            args[0] = cast<path> (rs["config.cxx"]).string ().c_str ();

            // Add the option that triggers building a shared library and take
            // care of any extras (e.g., import library).
            //
            if (lt == otype::s)
            {
              if (tclass == "macosx")
                args.push_back ("-dynamiclib");
              else
                args.push_back ("-shared");

              if (tsys == "mingw32")
              {
                // On Windows libs{} is the DLL and its first ad hoc group
                // member is the import library.
                //
                auto imp (static_cast<file*> (t.member));
                out = "-Wl,--out-implib=" + relative (imp->path ()).string ();
                args.push_back (out.c_str ());
              }
            }

            args.push_back ("-o");
            args.push_back (relt.string ().c_str ());
          }

          break;
        }
      }

      for (target* pt: t.prerequisite_targets)
      {
        file* f;
        liba* a (nullptr);
        libs* s (nullptr);

        if ((f = pt->is_a<obje> ()) ||
            (f = pt->is_a<obja> ()) ||
            (f = pt->is_a<objs> ()) ||
            (lt != otype::a &&
             ((f = a = pt->is_a<liba> ()) ||
              (f = s = pt->is_a<libs> ()))))
        {
          // On Windows a shared library is a DLL with the import library as a
          // first ad hoc group member. MinGW though can link directly to DLLs
          // (see search_library() for details).
          //
          if (s != nullptr && tclass == "windows")
          {
            if (s->member != nullptr)
              f = static_cast<file*> (s->member);
          }

          sargs.push_back (relative (f->path ()).string ()); // string()&&

          // If this is a static library, link all the libraries it depends
          // on, recursively.
          //
          if (a != nullptr)
            append_libraries (sargs, *a);
        }
      }

      // For MinGW manifest is an object file.
      //
      if (!manifest.empty () && tsys == "mingw32")
        sargs.push_back (relative (manifest).string ());

      // Copy sargs to args. Why not do it as we go along pushing into sargs?
      // Because of potential reallocations.
      //
      for (size_t i (0); i != sargs.size (); ++i)
        args.push_back (sargs[i].c_str ());

      if (lt != otype::a)
        append_options (args, t, "cxx.libs");

      args.push_back (nullptr);

      if (verb >= 2)
        print_process (args);
      else if (verb)
        text << "ld " << t;

      try
      {
        // VC++ (cl.exe, lib.exe, and link.exe) sends diagnostics to stdout.
        // To fix this (and any other insane compilers that may want to do
        // something like this) we are going to always redirect stdout to
        // stderr. For sane compilers this should be harmless.
        //
        process pr (args.data (), 0, 2);

        if (!pr.wait ())
          throw failed ();
      }
      catch (const process_error& e)
      {
        error << "unable to execute " << args[0] << ": " << e.what ();

        // In a multi-threaded program that fork()'ed but did not exec(),
        // it is unwise to try to do any kind of cleanup (like unwinding
        // the stack and running destructors).
        //
        if (e.child ())
          exit (1);

        throw failed ();
      }

      // Remove the target file if any of the subsequent actions fail. If we
      // don't do that, we will end up with a broken build that is up-to-date.
      //
      auto_rmfile rm (t.path ());

      if (ranlib)
      {
        const char* args[] = {
          cast<path> (ranlib).string ().c_str (),
          relt.string ().c_str (),
          nullptr};

        if (verb >= 2)
          print_process (args);

        try
        {
          process pr (args);

          if (!pr.wait ())
            throw failed ();
        }
        catch (const process_error& e)
        {
          error << "unable to execute " << args[0] << ": " << e.what ();

          if (e.child ())
            exit (1);

          throw failed ();
        }
      }

      // For Windows generate rpath-emulating assembly (unless updaing for
      // install).
      //
      if (lt == otype::e && tclass == "windows")
      {
        if (a.outer_operation () != install_id)
          windows_rpath_assembly (t, rpath_timestamp, scratch);
      }

      rm.cancel ();

      // Should we go to the filesystem and get the new mtime? We know the
      // file has been modified, so instead just use the current clock time.
      // It has the advantage of having the subseconds precision.
      //
      t.mtime (system_clock::now ());
      return target_state::changed;
    }

    target_state link::
    perform_clean (action a, target& xt)
    {
      file& t (static_cast<file&> (xt));

      scope& rs (t.root_scope ());
      const string& tsys (cast<string> (rs["cxx.target.system"]));
      const string& tclass (cast<string> (rs["cxx.target.class"]));

      initializer_list<const char*> e;

      switch (link_type (t))
      {
      case otype::a:
        {
          e = {".d"};
          break;
        }
      case otype::e:
        {
          if (tclass == "windows")
          {
            if (tsys == "mingw32")
            {
              e = {".d", "/.dlls", ".manifest.o", ".manifest"};
            }
            else
            {
              // Assuming it's VC or alike. Clean up .ilk in case the user
              // enabled incremental linking (note that .ilk replaces .exe).
              //
              e = {".d", "/.dlls", ".manifest", "-.ilk"};
            }
          }
          else
            e = {".d"};

          break;
        }
      case otype::s:
        {
          if (tclass == "windows" && tsys != "mingw32")
          {
            // Assuming it's VC or alike. Clean up .exp and .ilk.
            //
            e = {".d", ".exp", "-.ilk"};
          }
          else
            e = {".d"};

          break;
        }
      }

      return clean_extra (a, t, e);
    }

    link link::instance;
  }
}
