// file      : build2/test/script/script.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2016 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <build2/test/script/script>

#include <build2/target>

using namespace std;

namespace build2
{
  namespace test
  {
    namespace script
    {
      // Quote if empty or contains spaces or any of the special characters.
      //
      // @@ What if it contains quotes, escapes?
      //
      static void
      to_stream_q (ostream& o, const string& s)
      {
        if (s.empty () || s.find_first_of (" |&<>=") != string::npos)
          o << '"' << s << '"';
        else
          o << s;
      };

      void
      to_stream (ostream& o, const command& c, command_to_stream m)
      {
        auto print_redirect = [&o] (const redirect& r, const char* prefix)
        {
          o << ' ' << prefix;

          size_t n (string::traits_type::length (prefix));
          assert (n > 0);

          switch (r.type)
          {
          case redirect_type::none:        assert (false);           break;
          case redirect_type::null:        o << '!';                 break;
          case redirect_type::here_string: to_stream_q (o, r.value); break;
          case redirect_type::here_document:
            {
              // Add another '>' or '<'. Note that here end marker never
              // needs to be quoted.
              //
              o << prefix[n - 1] << r.here_end;
              break;
            }
          }
        };

        auto print_doc = [&o] (const redirect& r)
        {
          // Here-document value always ends with a newline.
          //
          o << endl << r.value << r.here_end;
        };

        if ((m & command_to_stream::header) == command_to_stream::header)
        {
          // Program.
          //
          to_stream_q (o, c.program.string ());

          // Arguments.
          //
          for (const string& a: c.arguments)
          {
            o << ' ';
            to_stream_q (o, a);
          }

          // Redirects.
          //
          if (c.in.type  != redirect_type::none) print_redirect (c.in,   "<");
          if (c.out.type != redirect_type::none) print_redirect (c.out,  ">");
          if (c.err.type != redirect_type::none) print_redirect (c.err, "2>");
        }

        if ((m & command_to_stream::here_doc) == command_to_stream::here_doc)
        {
          // Here-documents.
          //
          if (c.in.type  == redirect_type::here_document) print_doc (c.in);
          if (c.out.type == redirect_type::here_document) print_doc (c.out);
          if (c.err.type == redirect_type::here_document) print_doc (c.err);
        }
      }

      ostream&
      operator<< (ostream& o, const test& t)
      {
        to_stream (o, t, command_to_stream::header);

        if (t.exit.comparison != exit_comparison::eq || t.exit.status != 0)
        {
          switch (t.exit.comparison)
          {
          case exit_comparison::eq: o << " == "; break;
          case exit_comparison::ne: o << " != "; break;
          }

          o << static_cast<uint16_t> (t.exit.status);
        }

        to_stream (o, t, command_to_stream::here_doc);
        return o;
      }

      script::
      script (target& tt, testscript& st)
          : test_target (tt), script_target (st),

            // Enter the test* variables with the same variable types as in
            // buildfiles.
            //
            test_var (var_pool.insert<path> ("test")),
            opts_var (var_pool.insert<strings> ("test.options")),
            args_var (var_pool.insert<strings> ("test.arguments")),

            cmd_var (var_pool.insert<strings> ("*")),
            cwd_var (var_pool.insert<dir_path> ("~"))
      {
        // Unless we have the test variable set on the test or script target,
        // set it at the script level to the test target's path.
        //
        if (!find (test_var))
        {
          value& v (assign (test_var));

          // If this is a path-based target, then we use the path. If this
          // is a directory (alias) target, then we use the directory path.
          // Otherwise, we leave it NULL expecting the testscript to set it
          // to something appropriate, if used.
          //
          if (auto* p = tt.is_a<path_target> ())
            v = p->path ();
          else if (tt.is_a<dir> ())
            v = path (tt.dir.string ()); // Strip trailing slash.
        }

        // Also add the NULL $* value that signals it needs to be recalculated
        // on first access.
        //
        assign (cmd_var) = nullptr;

        // Set the script CWD ($~) which is the $out_base/<script-name>.
        //
        // @@ This will conflict for 'testscript'!
        //
        assign (cwd_var) = dir_path (tt.out_dir ()) /= st.name;
      }

      lookup scope::
      find (const variable& var) const
      {
        // Search script scopes until we hit the root.
        //
        const scope* p (this);

        do
        {
          if (const value* v = p->vars.find (var))
            return lookup (v, &p->vars);
        }
        while (p->parent != nullptr ? (p = p->parent) : nullptr);

        // Switch to the corresponding buildfile variable. Note that we don't
        // want to insert a new variable into the pool (we might be running
        // concurrently). Plus, if there is no such variable, then we cannot
        // possibly find any value.
        //
        const variable* pvar (build2::var_pool.find (var.name));

        if (pvar == nullptr)
          return lookup ();

        const script& s (static_cast<const script&> (*p));
        {
          const variable& var (*pvar);

          // First check the target we are testing.
          //
          {
            // Note that we skip applying the override if we did not find any
            // value. In this case, presumably the override also affects the
            // script target and we will pick it up there. A bit fuzzy.
            //
            auto p (s.test_target.find_original (var, true));

            if (p.first)
            {
              if (var.override != nullptr)
                p = s.test_target.base_scope ().find_override (
                  var, move (p), true);

              return p.first;
            }
          }

          // Then the script target followed by the scopes it is in. Note that
          // while unlikely it is possible the test and script targets will be
          // in different scopes which brings the question of which scopes we
          // should search.
          //
          return s.script_target[var];
        }
      }

      value& scope::
      append (const variable& var)
      {
        lookup l (find (var));

        if (l.defined () && l.belongs (*this)) // Existing var in this scope.
          return const_cast<value&> (*l);

        value& r (assign (var)); // NULL.

        //@@ I guess this is where we convert untyped value to strings?
        //
        if (l.defined ())
          r = *l; // Copy value (and type) from the outer scope.

        return r;
      }
    }
  }
}
