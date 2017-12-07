// file      : build2/scope.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <build2/scope.hxx>

#include <build2/target.hxx>
#include <build2/context.hxx>

using namespace std;

namespace build2
{
  // scope
  //
  pair<lookup, size_t> scope::
  find_original (const variable& var,
                 const target_type* tt, const string* tn,
                 const target_type* gt, const string* gn,
                 size_t start_d) const
  {
    assert (tt != nullptr || var.visibility != variable_visibility::target);

    size_t d (0);

    // Process target type/pattern-specific prepend/append values.
    //
    auto pre_app = [&var] (lookup& l,
                           const scope* s,
                           const target_type* tt, const string* tn,
                           const target_type* gt, const string* gn)
    {
      const value& v (*l);
      assert ((v.extra == 1 || v.extra == 2) && v.type == nullptr);

      // First we need to look for the stem value starting from the "next
      // lookup point". That is, if we have the group, then from the
      // s->target_vars (for the group), otherwise from s->vars, and then
      // continuing looking in the outer scopes (for both target and group).
      // Note that this may have to be repeated recursively, i.e., we may have
      // prepents/appends in outer scopes. Also, if the value is for the
      // group, then we shouldn't be looking for stem in the target's
      // variables. In other words, once we "jump" to group, we stay there.
      //
      lookup stem (s->find_original (var, tt, tn, gt, gn, 2).first);

      // Check the cache.
      //
      pair<value&, ulock> entry (
        s->target_vars.cache.insert (
          make_tuple (&v, tt, *tn),
          stem,
          static_cast<const variable_map::value_data&> (v).version,
          var));

      value& cv (entry.first);

      // If cache miss/invalidation, update the value.
      //
      if (entry.second.owns_lock ())
      {
        // Un-typify the cache. This can be necessary, for example, if we are
        // changing from one value-typed stem to another.
        //
        // Note: very similar logic as in the override cache population code
        // below.
        //
        if (!stem.defined () || cv.type != stem->type)
        {
          cv = nullptr;
          cv.type = nullptr; // Un-typify.
        }

        // Copy the stem.
        //
        if (stem.defined ())
          cv = *stem;

        // Typify the cache value in case there is no stem (we still want to
        // prepend/append things in type-aware way).
        //
        if (cv.type == nullptr && var.type != nullptr)
          typify (cv, *var.type, &var);

        // Now prepend/append the value, unless it is NULL.
        //
        if (v)
        {
          if (v.extra == 1)
            cv.prepend (names (cast<names> (v)), &var);
          else
            cv.append (names (cast<names> (v)), &var);
        }
      }

      // Return cache as the resulting value but retain l.vars, so it looks as
      // if the value came from s->target_vars.
      //
      l.value = &cv;
    };

    for (const scope* s (this); s != nullptr; )
    {
      if (tt != nullptr) // This started from the target.
      {
        bool f (!s->target_vars.empty ());

        // Target.
        //
        if (++d >= start_d)
        {
          if (f)
          {
            lookup l (s->target_vars.find (*tt, *tn, var));

            if (l.defined ())
            {
              if (l->extra != 0) // Prepend/append?
                pre_app (l, s, tt, tn, gt, gn);

              return make_pair (move (l), d);
            }
          }
        }

        // Group.
        //
        if (++d >= start_d)
        {
          if (f && gt != nullptr)
          {
            lookup l (s->target_vars.find (*gt, *gn, var));

            if (l.defined ())
            {
              if (l->extra != 0) // Prepend/append?
                pre_app (l, s, gt, gn, nullptr, nullptr);

              return make_pair (move (l), d);
            }
          }
        }
      }

      // Note that we still increment the lookup depth so that we can compare
      // depths of variables with different visibilities.
      //
      if (++d >= start_d && var.visibility != variable_visibility::target)
      {
        if (const value* v = s->vars.find (var))
          return make_pair (lookup (v, &s->vars), d);
      }

      switch (var.visibility)
      {
      case variable_visibility::scope:
        s = nullptr;
        break;
      case variable_visibility::target:
      case variable_visibility::project:
        s = s->root () ? nullptr : s->parent_scope ();
        break;
      case variable_visibility::normal:
        s = s->parent_scope ();
        break;
      }
    }

    return make_pair (lookup (), size_t (~0));
  }

  pair<lookup, size_t> scope::
  find_override (const variable& var,
                 pair<lookup, size_t> original,
                 bool target) const
  {
    // Normally there would be no overrides and if there are, there will only
    // be a few of them. As a result, here we concentrate on keeping the logic
    // as straightforward as possible without trying to optimize anything.
    //
    // Note also that we rely (e.g., in the config module) on the fact that if
    // no overrides apply, then we return the original value and not its copy
    // in the cache (this is used to detect if the value was overriden).
    //
    assert (var.override != nullptr);

    const lookup& orig (original.first);
    size_t orig_depth (original.second);

    // The first step is to find out where our cache will reside. After some
    // meditation you will see it should be next to the innermost (scope-wise)
    // value of this variable (override or original).
    //
    // We also keep track of the root scope of the project from which this
    // innermost value comes. This is used to decide whether a non-recursive
    // project-wise override applies. And also where our variable cache is.
    //
    const variable_map* inner_vars (nullptr);
    const scope* inner_proj (nullptr);

    // One special case is if the original is target-specific, which is the
    // most innermost. Or is it innermostest?
    //
    bool targetspec (false);
    if (target)
    {
      targetspec = orig.defined () && (orig_depth == 1 || orig_depth == 2);

      if (targetspec)
      {
        inner_vars = orig.vars;
        inner_proj = root_scope ();
      }
    }

    const scope* s;

    // Return true if the override applies. Note that it expects vars and proj
    // to be not NULL; if there is nothing "more inner", then any override
    // will still be "visible".
    //
    auto applies = [&s] (const variable* o,
                         const variable_map* vars,
                         const scope* proj) -> bool
    {
      switch (o->visibility)
      {
      case variable_visibility::scope:
      {
        // Does not apply if in a different scope.
        //
        if (vars != &s->vars)
          return false;

        break;
      }
      case variable_visibility::project:
      {
        // Does not apply if in a different project.
        //
        if (proj != s->root_scope ())
          return false;

        break;
      }
      case variable_visibility::normal:
        break;
      case variable_visibility::target:
        assert (false);
      }

      return true;
    };

    // Return the override value if it is present and (optionally) ends with
    // a suffix.
    //
    auto find = [&s] (const variable* o, const char* sf = nullptr) -> lookup
    {
      if (sf != nullptr && o->name.rfind (sf) == string::npos)
        return lookup ();

      return lookup (s->vars.find (*o), &s->vars);
    };

    // Return true if a value is from this scope (either target type/pattern-
    // specific or ordinary).
    //
    auto belongs = [&s, target] (const lookup& l) -> bool
    {
      if (target)
      {
        for (auto& p1: s->target_vars)
          for (auto& p2: p1.second)
            if (l.vars == &p2.second)
              return true;
      }

      return l.vars == &s->vars;
    };

    // While looking for the cache we also detect if none of the overrides
    // apply. In this case the result is simply the original value (if any).
    //
    bool apply (false);

    for (s = this; s != nullptr; s = s->parent_scope ())
    {
      // If we are still looking for the cache, see if the original comes from
      // this scope. We check this before the overrides since it can come from
      // the target type/patter-specific variables, which is "more inner" than
      // normal scope variables (see find_original()).
      //
      if (inner_vars == nullptr && orig.defined () && belongs (orig))
      {
        inner_vars = orig.vars;
        inner_proj = s->root_scope ();
      }

      for (const variable* o (var.override.get ());
           o != nullptr;
           o = o->override.get ())
      {
        if (inner_vars != nullptr && !applies (o, inner_vars, inner_proj))
          continue;

        auto l (find (o));

        if (l.defined ())
        {
          if (inner_vars == nullptr)
          {
            inner_vars = l.vars;
            inner_proj = s->root_scope ();
          }

          apply = true;
          break;
        }
      }

      // We can stop if we found the cache and at least one override applies.
      //
      if (inner_vars != nullptr && apply)
        break;
    }

    if (!apply)
      return original;

    assert (inner_vars != nullptr);

    // If for some reason we are not in a project, use the cache from the
    // global scope.
    //
    if (inner_proj == nullptr)
      inner_proj = global_scope;

    // Now find our "stem", that is, the value to which we will be appending
    // suffixes and prepending prefixes. This is either the original or the
    // __override, provided it applies. We may also not have either.
    //
    lookup stem;
    size_t stem_depth (0);
    const scope* stem_proj (nullptr);

    // Again the special case of a target-specific variable.
    //
    if (targetspec)
    {
      stem = orig;
      stem_depth = orig_depth;
      stem_proj = root_scope ();
    }

    size_t ovr_depth (target ? 2 : 0); // For implied target-specific lookup.

    for (s = this; s != nullptr; s = s->parent_scope ())
    {
      bool done (false);

      // First check if the original is from this scope.
      //
      if (orig.defined () && belongs (orig))
      {
        stem = orig;
        stem_depth = orig_depth;
        stem_proj = s->root_scope ();
        // Keep searching.
      }

      ++ovr_depth;

      // Then look for an __override that applies.
      //
      for (const variable* o (var.override.get ());
           o != nullptr;
           o = o->override.get ())
      {
        // If we haven't yet found anything, then any override will still be
        // "visible" even if it doesn't apply.
        //
        if (stem.defined () && !applies (o, stem.vars, stem_proj))
          continue;

        auto l (find (o, ".__override"));

        if (l.defined ())
        {
          stem = move (l);
          stem_depth = ovr_depth;
          stem_proj = s->root_scope ();
          done = true;
          break;
        }
      }

      if (done)
        break;
    }

    // Check the cache.
    //
    pair<value&, ulock> entry (
      inner_proj->override_cache.insert (
        make_pair (&var, inner_vars),
        stem,
        0, // Overrides are immutable.
        var));

    value& cv (entry.first);
    bool cl (entry.second.owns_lock ());

    // If cache miss/invalidation, update the value.
    //
    if (cl)
    {
      // Note: very similar logic as in the target type/pattern specific cache
      // population code above.
      //

      // Un-typify the cache. This can be necessary, for example, if we are
      // changing from one value-typed stem to another.
      //
      if (!stem.defined () || cv.type != stem->type)
      {
        cv = nullptr;
        cv.type = nullptr; // Un-typify.
      }

      if (stem.defined ())
        cv = *stem;

      // Typify the cache value. If the stem is the original, then the type
      // would get propagated automatically. But the stem could also be the
      // override, which is kept untyped. Or the stem might not be there at
      // all while we still need to apply prefixes/suffixes in the type-aware
      // way.
      //
      if (cv.type == nullptr && var.type != nullptr)
        typify (cv, *var.type, &var);
    }

    // Now apply override prefixes and suffixes (if updating the cache). Also
    // calculate the vars and depth of the result, which will be those of the
    // stem or prefix/suffix that applies, whichever is the innermost.
    //
    // Note: we could probably cache this information instead of recalculating
    // it every time.
    //
    size_t depth (stem_depth);
    const variable_map* vars (stem.vars);
    const scope* proj (stem_proj);

    ovr_depth = target ? 2 : 0;

    for (s = this; s != nullptr; s = s->parent_scope ())
    {
      ++ovr_depth;

      for (const variable* o (var.override.get ());
           o != nullptr;
           o = o->override.get ())
      {
        // First see if this override applies. This is tricky: what if the
        // stem is a "visible" override from an outer project?  Shouldn't its
        // overrides apply? Sure sounds logical. So we use the project of the
        // stem's scope.
        //
        if (vars != nullptr && !applies (o, vars, proj))
          continue;

        // Note that we keep override values as untyped names even if the
        // variable itself is typed. We also pass the original variable for
        // diagnostics.
        //
        auto l (find (o, ".__prefix"));

        if (cl)
        {
          if (l) // No sense to prepend/append if NULL.
          {
            cv.prepend (names (cast<names> (l)), &var);
          }
          else if ((l = find (o, ".__suffix")))
          {
            cv.append (names (cast<names> (l)), &var);
          }
        }

        if (l.defined ())
        {
          // If we had no stem, use the first override as a surrogate stem.
          //
          if (vars == nullptr)
          {
            depth = ovr_depth;
            vars = &s->vars;
            proj = s->root_scope ();
          }
          // Otherwise, pick the innermost location between the stem and
          // prefix/suffix.
          //
          else if (ovr_depth < depth)
          {
            depth = ovr_depth;
            vars = &s->vars;
          }
        }
      }
    }

    // Use the location of the innermost value that contributed as the
    // location of the result.
    //
    return make_pair (lookup (&cv, vars), depth);
  }

  value& scope::
  append (const variable& var)
  {
    // Note that here we want the original value without any overrides
    // applied.
    //
    lookup l (find_original (var).first);

    if (l.defined () && l.belongs (*this)) // Existing var in this scope.
      return vars.modify (l); // Ok since this is original.

    value& r (assign (var)); // NULL.

    if (l.defined ())
      r = *l; // Copy value (and type) from the outer scope.

    return r;
  }

  const target_type* scope::
  find_target_type (const string& tt, const scope** rs) const
  {
    // Search scopes outwards, stopping at the project root.
    //
    for (const scope* s (this);
         s != nullptr;
         s = s->root () ? global_scope : s->parent_scope ())
    {
      if (s->target_types.empty ())
        continue;

      auto i (s->target_types.find (tt));

      if (i != s->target_types.end ())
      {
        if (rs != nullptr)
          *rs = s;

        return &i->second.get ();
      }
    }

    return nullptr;
  }

  static const string dir_tt ("dir");
  static const string file_tt ("file");

  const target_type* scope::
  find_target_type (name& n, optional<string>& ext) const
  {
    ext = nullopt;

    string& v (n.value);

    // First determine the target type.
    //
    const string* tt;
    if (n.untyped ())
    {
      // Empty name or '.' and '..' signify a directory.
      //
      if (v.empty () || v == "." || v == "..")
        tt = &dir_tt;
      else
        //@@ TODO: derive type from extension.
        //
        tt = &file_tt;
    }
    else
      tt = &n.type;

    const target_type* r (find_target_type (*tt));

    if (r == nullptr)
      return r;

    // Directories require special name processing. If we find that more
    // targets deviate, then we should make this target-type-specific.
    //
    if (r->is_a<dir> () || r->is_a<fsdir> ())
    {
      // The canonical representation of a directory name is with empty
      // value.
      //
      if (!v.empty ())
      {
        n.dir /= dir_path (v); // Move name value to dir.
        v.clear ();
      }
    }
    else if (!v.empty ())
    {
      // Split the path into its directory part (if any) the name part,
      // and the extension (if any). We cannot assume the name part is
      // a valid filesystem name so we will have to do the splitting
      // manually.
      //
      path::size_type i (path::traits::rfind_separator (v));

      if (i != string::npos)
      {
        n.dir /= dir_path (v, i != 0 ? i : 1); // Special case: "/".
        v = string (v, i + 1, string::npos);
      }

      // Extract the extension. Treat trailing dot as specified but empty
      // extension.
      //
      string::size_type j (v.back () != '.'
                           ? path::traits::find_extension (v)
                           : v.size () - 1);

      if (j != string::npos)
      {
        ext = string (v.c_str () + j + 1);
        v.resize (j);
      }
    }

    return r;
  }

  static target*
  derived_tt_factory (const target_type& t, dir_path d, dir_path o, string n)
  {
    // Pass our type to the base factory so that it can detect that it is
    // being called to construct a derived target. This can be used, for
    // example, to decide whether to "link up" to the group.
    //
    // One exception: if we are derived from a derived target type, then this
    // logic would lead to infinite recursion. So in this case get the
    // ultimate base.
    //
    const target_type* bt (t.base);
    for (; bt->factory == &derived_tt_factory; bt = bt->base) ;

    target* r (bt->factory (t, move (d), move (o), move (n)));
    r->derived_type = &t;
    return r;
  }

  pair<reference_wrapper<const target_type>, bool> scope::
  derive_target_type (const string& name, const target_type& base)
  {
    // Base target type uses extensions.
    //
    bool ext (base.fixed_extension   != nullptr ||
              base.default_extension != nullptr);

    // @@ Looks like we may need the ability to specify a fixed extension
    //    (which will be used to compare existing targets and not just
    //    search for existing files that is handled by the target_type::
    //    extension hook). See the file_factory() for details. We will
    //    probably need to specify it as part of the define directive (and
    //    have the ability to specify empty and NULL).
    //
    //    Currently, if we define myfile{}: file{}, then myfile{foo} and
    //    myfile{foo.x} are the same target.
    //
    unique_ptr<target_type> dt (new target_type (base));
    dt->base = &base;
    dt->factory = &derived_tt_factory;

    // @@ We should probably inherit the fixed extension unless overriden with
    // another fixed? But then any derivation from file{} will have to specify
    // (or override) the fixed extension? But what is the use of deriving from
    // a fixed extension target and not overriding its extension? Some kind of
    // alias. Fuzzy.
    //
    dt->fixed_extension = nullptr /*&target_extension_fix<???>*/; // @@ TODO

    // Override default extension/pattern derivation function: we most likely
    // don't want to use the same default as our base (think cli: file). But,
    // if our base doesn't use extensions, then most likely neither do we
    // (think foo: alias).
    //
    dt->default_extension =
      ext && dt->fixed_extension == nullptr
      ? &target_extension_var<var_extension, nullptr>
      : nullptr;

    dt->pattern =
      dt->fixed_extension != nullptr ? nullptr /*&target_pattern_fix<???>*/ :
      dt->default_extension != nullptr ? &target_pattern_var<var_extension, nullptr> :
      nullptr;

    // There is actually a difference between "fixed fixed" (like man1{}) and
    // "fixed but overridable" (like file{}). Fuzzy: feels like there are
    // different kinds of "fixed" (file{} vs man{} vs man1{}).
    //
    dt->print =
      dt->fixed_extension != nullptr
      ? &target_print_0_ext_verb  // Fixed extension, no use printing.
      : nullptr;                  // Normal.

    target_type& rdt (*dt); // Save a non-const reference to the object.

    auto pr (target_types.emplace (name, target_type_ref (move (dt))));

    // Patch the alias name to use the map's key storage.
    //
    if (pr.second)
      rdt.name = pr.first->first.c_str ();

    return pair<reference_wrapper<const target_type>, bool> (
      pr.first->second.get (), pr.second);
  }

  scope* scope::global_;

  // scope_map
  //
  scope_map scope_map::instance;
  const scope_map& scope_map::cinstance = scope_map::instance;
  const scope_map& scopes = scope_map::cinstance;

  const scope* global_scope;

  auto scope_map::
  insert (const dir_path& k, bool root) -> iterator
  {
    scope_map_base& m (*this);

    auto er (m.emplace (k, scope (true))); // Global.
    scope& s (er.first->second);

    // If this is a new scope, update the parent chain.
    //
    if (er.second)
    {
      scope* p (nullptr);

      // Update scopes of which we are a new parent/root (unless this is the
      // global scope). Also find our parent while at it.
      //
      if (m.size () > 1)
      {
        // The first entry is ourselves.
        //
        auto r (m.find_prefix (k));
        for (++r.first; r.first != r.second; ++r.first)
        {
          scope& c (r.first->second);

          // The first scope of which we are a parent is the least (shortest)
          // one which means there is no other scope between it and our
          // parent.
          //
          if (p == nullptr)
            p = c.parent_;

          if (root && c.root_ == p->root_) // No intermediate root.
            c.root_ = &s;

          if (p == c.parent_) // No intermediate parent.
            c.parent_ = &s;
        }

        // We couldn't get the parent from one of its old children so we have
        // to find it ourselves.
        //
        if (p == nullptr)
          p = &find (k.directory ());
      }

      s.parent_ = p;
      s.root_ = root ? &s : (p != nullptr ? p->root_ : nullptr);
    }
    else if (root && !s.root ())
    {
      // Upgrade to root scope.
      //
      auto r (m.find_prefix (k));
      for (++r.first; r.first != r.second; ++r.first)
      {
        scope& c (r.first->second);

        if (c.root_ == s.root_) // No intermediate root.
          c.root_ = &s;
      }

      s.root_ = &s;
    }

    return er.first;
  }

  scope& scope_map::
  find (const dir_path& k)
  {
    scope_map_base& m (*this);

    // Better implementation that should work but doesn't.
    //
#if 0
    assert (k.normalized (false)); // Allow non-canonical dir separators.
    auto i (m.find_sup (k));
    return i != m.end () ? i->second : const_cast<scope&> (*global_scope);
#else
   // Normally we would have a scope for the full path so try that before
   // making any copies.
   //
   auto i (m.find (k)), e (m.end ());

   if (i != e)
     return i->second;

   for (dir_path d (k.directory ());; d = d.directory ())
   {
     auto i (m.find (d));

     if (i != e)
       return i->second;

     assert (!d.empty ()); // We should have the global scope.
   }
#endif
  }
}
