// file      : build/algorithm.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2015 Code Synthesis Tools CC
// license   : MIT; see accompanying LICENSE file

#include <build/algorithm>

#include <memory>   // unique_ptr
#include <utility>  // move
#include <cassert>

#include <build/path>
#include <build/scope>
#include <build/target>
#include <build/prerequisite>
#include <build/rule>
#include <build/search>
#include <build/utility>
#include <build/diagnostics>

using namespace std;

namespace build
{
  target&
  search (const prerequisite_key& pk)
  {
    if (target* t = pk.tk.type->search (pk))
      return *t;

    return create_new_target (pk);
  }

  void
  match_impl (action a, target& t)
  {
    for (auto tt (&t.type ());
         tt != nullptr && !t.recipe (a);
         tt = tt->base)
    {
      auto i (current_rules->find (tt->id));

      if (i == current_rules->end () || i->second.empty ())
        continue; // No rules registered for this target type, try base.

      const auto& rules (i->second); // Hint map.

      // @@ TODO
      //
      // Different rules can be used for different operations (update
      // vs test is a good example). So, at some point, we will probably
      // have to support a list of hints or even an operation-hint map
      // (e.g., 'hint=cxx test=foo' if cxx supports the test operation
      // but we want the foo rule instead). This is also the place where
      // the '{build clean}=cxx' construct (which we currently do not
      // support) can come handy.
      //
      // Also, ignore the hint (that is most likely ment for a different
      // operation) if this is a unique match.
      //
      string hint;
      auto rs (rules.size () == 1
               ? make_pair (rules.begin (), rules.end ())
               : rules.find_prefix (hint));

      for (auto i (rs.first); i != rs.second; ++i)
      {
        const string& n (i->first);
        const rule& ru (i->second);

        void* m (nullptr);
        {
          auto g (
            make_exception_guard (
              [](action a, target& t, const string& n)
              {
                info << "while matching rule " << n << " to "
                     << diag_do (a, t);
              },
              a, t, n));

          m = ru.match (a, t, hint);
        }

        if (m != nullptr)
        {
          // Do the ambiguity test.
          //
          bool ambig (false);

          diag_record dr;

          for (++i; i != rs.second; ++i)
          {
            const string& n1 (i->first);
            const rule& ru1 (i->second);

            void* m1;
            {
              auto g (
                make_exception_guard (
                  [](action a, target& t, const string& n1)
                  {
                    info << "while matching rule " << n1 << " to "
                         << diag_do (a, t);
                  },
                  a, t, n1));

              m1 = ru1.match (a, t, hint);
            }

            if (m1 != nullptr)
            {
              if (!ambig)
              {
                dr << fail << "multiple rules matching " << diag_doing (a, t)
                   << info << "rule " << n << " matches";
                ambig = true;
              }

              dr << info << "rule " << n1 << " also matches";
            }
          }

          if (!ambig)
          {
            auto g (
              make_exception_guard (
                [](action a, target& t, const string& n)
                {
                  info << "while applying rule " << n << " to "
                       << diag_do (a, t);
                },
                a, t, n));

            t.recipe (a, ru.apply (a, t, m));
            break;
          }
          else
            dr << info << "use rule hint to disambiguate this match";
        }
      }
    }

    if (!t.recipe (a))
      fail << "no rule to " << diag_do (a, t);
  }

  void
  search_and_match (action a, target& t)
  {
    if (t.group != nullptr)
      search_and_match (a, *t.group);

    for (prerequisite_target& p: t.prerequisites)
    {
      p.target = &search (p);
      match (a, *p.target);
    }
  }

  void
  search_and_match (action a, target& t, const dir_path& d)
  {
    if (t.group != nullptr)
      search_and_match (a, *t.group, d);

    for (prerequisite_target& p: t.prerequisites)
    {
      p.target = &search (p);

      if (p.target->dir.sub (d))
        match (a, *p.target);
      else
        p.target = nullptr; // Ignore.
    }
  }

  void
  inject_parent_fsdir (action a, target& t)
  {
    tracer trace ("inject_parent_fsdir");

    scope& s (t.base_scope ());

    if (scope* rs = s.root_scope ()) // Could be outside any project.
    {
      const dir_path& out_root (rs->path ());

      // If t is a directory (name is empty), say foo/bar/, then
      // t is bar and its parent directory is foo/.
      //
      const dir_path& d (t.name.empty () ? t.dir.directory () : t.dir);

      if (d.sub (out_root) && d != out_root)
      {
        level5 ([&]{trace << "injecting prerequisite for " << t;});

        prerequisite& p (
          s.prerequisites.insert (
            fsdir::static_type,
            d,
            string (),
            nullptr,
            s,
            trace).first);

        target& pt (search (p));

        t.prerequisites.emplace_back (p, pt);

        match (a, pt);
      }
    }
  }

  target_state
  execute_impl (action a, target& t)
  {
    // Implementation with some multi-threading ideas in mind.
    //
    switch (target_state ts = t.state)
    {
    case target_state::unknown:
    case target_state::postponed:
      {
        t.state = target_state::failed; // So the rule can just throw.

        auto g (
          make_exception_guard (
            [](action a, target& t){info << "while " << diag_doing (a, t);},
            a, t));

        ts = t.recipe (a) (a, t);
        assert (ts != target_state::unknown && ts != target_state::failed);

        // The recipe may have set the target's state manually.
        //
        if (t.state == target_state::failed)
          t.state = ts;

        return ts;
      }
    case target_state::unchanged:
    case target_state::changed:
      // Should have been handled by inline execute().
      assert (false);
    case target_state::failed:
      throw failed ();
    }
  }

  target_state
  noop_action (action, target&)
  {
    assert (false); // We shouldn't be called, see target::recipe().
    return target_state::unchanged;
  }
}
