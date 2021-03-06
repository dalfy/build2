// file      : build2/scope.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BUILD2_SCOPE_HXX
#define BUILD2_SCOPE_HXX

#include <map>
#include <unordered_set>

#include <libbutl/path-map.mxx>

#include <build2/types.hxx>
#include <build2/utility.hxx>

#include <build2/module.hxx>
#include <build2/variable.hxx>
#include <build2/target-key.hxx>
#include <build2/target-type.hxx>
#include <build2/target-state.hxx>
#include <build2/rule-map.hxx>
#include <build2/operation.hxx>

namespace build2
{
  class dir;

  class scope
  {
  public:
    // Absolute and normalized.
    //
    const dir_path& out_path () const {return *out_path_;}
    const dir_path& src_path () const {return *src_path_;}

    // The first is a pointer to the key in scope_map. The second is a pointer
    // to the src_root/base variable value, if any (i.e., it can be NULL).
    //
    const dir_path* out_path_ = nullptr;
    const dir_path* src_path_ = nullptr;

    bool
    root () const {return root_ == this;}

    scope*       parent_scope ()       {return parent_;}
    const scope* parent_scope () const {return parent_;}

    // Root scope of this scope or NULL if this scope is not (yet)
    // in any (known) project. Note that if the scope itself is
    // root, then this function return this. To get to the outer
    // root, query the root scope of the parent.
    //
    scope*       root_scope ()       {return root_;}
    const scope* root_scope () const {return root_;}

    // Root scope of a strong amalgamation of this scope or NULL if
    // this scope is not (yet) in any (known) project. If there is
    // no strong amalgamation, then this function returns the root
    // scope of the project (in other words, in this case a project
    // is treated as its own strong amalgamation).
    //
    scope*       strong_scope ();
    const scope* strong_scope () const;

    // Root scope of the outermost amalgamation or NULL if this scope is not
    // (yet) in any (known) project. If there is no amalgamation, then this
    // function returns the root scope of the project (in other words, in this
    // case a project is treated as its own amalgamation).
    //
    scope*       weak_scope ();
    const scope* weak_scope () const;

    // Return true if the specified root scope is a sub-scope of this root
    // scope. Note that both scopes must be root.
    //
    bool
    sub_root (const scope&) const;

    // Variables.
    //
  public:
    variable_map vars;

    // Lookup, including in outer scopes. If you only want to lookup in this
    // scope, do it on the the variables map directly (and note that there
    // will be no overrides).
    //
    lookup
    operator[] (const variable& var) const
    {
      return find (var).first;
    }

    lookup
    operator[] (const variable* var) const // For cached variables.
    {
      assert (var != nullptr);
      return operator[] (*var);
    }

    lookup
    operator[] (const string& name) const
    {
      const variable* var (var_pool.find (name));
      return var != nullptr ? operator[] (*var) : lookup ();
    }

    // As above, but include target type/pattern-specific variables.
    //
    lookup
    find (const variable& var, const target_key& tk) const
    {
      return find (var, tk.type, tk.name).first;
    }

    lookup
    find (const variable& var, const target_type& tt, const string& tn) const
    {
      return find (var, &tt, &tn).first;
    }

    pair<lookup, size_t>
    find (const variable& var,
          const target_type* tt = nullptr,
          const string* tn = nullptr) const
    {
      auto p (find_original (var, tt, tn));
      return var.override == nullptr ? p : find_override (var, move (p));
    }

    // Implementation details (used by scope target lookup). The start_depth
    // can be used to skip a number of initial lookups.
    //
    pair<lookup, size_t>
    find_original (
      const variable&,
      const target_type* tt = nullptr, const string* tn = nullptr,
      const target_type* gt = nullptr, const string* gn = nullptr,
      size_t start_depth = 1) const;

    pair<lookup, size_t>
    find_override (const variable&,
                   pair<lookup, size_t> original,
                   bool target = false,
                   bool rule = false) const;

    // Return a value suitable for assignment (or append if you only want to
    // append to the value from this scope). If the value does not exist in
    // this scope's map, then a new one with the NULL value is added and
    // returned. Otherwise the existing value is returned.
    //
    value&
    assign (const variable& var) {return vars.assign (var);}

    value&
    assign (const variable* var) {return vars.assign (var);} // For cached.

    value&
    assign (string name)
    {
      return assign (variable_pool::instance.insert (move (name)));
    }

    // Assign a typed non-overridable variable with normal visibility.
    //
    template <typename T>
    value&
    assign (string name)
    {
      return vars.assign (variable_pool::instance.insert<T> (move (name)));
    }

    // Return a value suitable for appending. If the variable does not
    // exist in this scope's map, then outer scopes are searched for
    // the same variable. If found then a new variable with the found
    // value is added to this scope and returned. Otherwise this
    // function proceeds as assign().
    //
    value&
    append (const variable&);

    // Target type/pattern-specific variables.
    //
    variable_type_map target_vars;

    // Variable override cache (project root and global scope only).
    //
    // The key is the variable plus the innermost (scope-wise) variable map to
    // which this override applies. See find_override() for details.
    //
    // Note: since it can be modified on any lookup (including during the
    // execute phase), the cache is protected by its own mutex shard.
    //
    mutable
    variable_cache<pair<const variable*, const variable_map*>>
    override_cache;

    // Meta/operations supported by this project (set on the root
    // scope only).
    //
  public:
    build2::meta_operations meta_operations;
    build2::operations operations;

    using path_type = build2::path;

    // Set of buildfiles already loaded for this scope. The included
    // buildfiles are checked against the project's root scope while
    // imported -- against the global scope (global_scope).
    //
    std::unordered_set<path_type> buildfiles;

    // Target types.
    //
  public:
    target_type_map target_types;

    const target_type*
    find_target_type (const string&, const scope** = nullptr) const;

    // Given a target name, figure out its type, taking into account
    // extensions, special names (e.g., '.' and '..'), or anything else that
    // might be relevant. Process the name (in place) by extracting (and
    // returning) extension, adjusting dir/leaf, etc., (note that the dir is
    // not necessarily normalized). Return NULL if not found.
    //
    pair<const target_type*, optional<string>>
    find_target_type (name&, const location&) const;

    // Dynamically derive a new target type from an existing one. Return the
    // reference to the target type and an indicator of whether it was
    // actually created.
    //
    pair<reference_wrapper<const target_type>, bool>
    derive_target_type (const string& name, const target_type& base);

    template <typename T>
    pair<reference_wrapper<const target_type>, bool>
    derive_target_type (const string& name)
    {
      return derive_target_type (name, T::static_type);
    }

    // Rules.
    //
  public:
    rule_map rules;

    // Operation callbacks.
    //
    // An entity (module, core) can register a function that will be called
    // when an action is executed on the dir{} target that corresponds to this
    // scope. The pre callback is called just before the recipe and the post
    // -- immediately after. The callbacks are only called if the recipe
    // (including noop recipe) is executed for the corresponding target. The
    // callbacks should only be registered during the load phase.
    //
    // It only makes sense for callbacks to return target_state changed or
    // unchanged and to throw failed in case of an error. These pre/post
    // states will be merged with the recipe state and become the target
    // state. See execute_recipe() for details.
    //
  public:
    struct operation_callback
    {
      using callback = target_state (action, const scope&, const dir&);

      function<callback> pre;
      function<callback> post;
    };

    using operation_callback_map = std::multimap<action_id,
                                                 operation_callback>;

    operation_callback_map operation_callbacks;

    // Modules.
    //
  public:
    loaded_module_map modules; // Only on root scope.

  public:
    // RW access.
    //
    scope&
    rw () const
    {
      assert (phase == run_phase::load);
      return const_cast<scope&> (*this);
    }

    // RW access to global scope (RO via global global_scope below).
    //
    scope&
    global () {return *global_;}

  public:
    static scope* global_; // Normally not accessed directly.

  private:
    friend class parser;
    friend class scope_map;
    friend class temp_scope;

    // These two from <build2/file.hxx> set strong_.
    //
    friend void create_bootstrap_outer (scope&);
    friend scope& create_bootstrap_inner (scope&, const dir_path&);

    explicit
    scope (bool global): vars (global), target_vars (global) {}

    scope* parent_;
    scope* root_;
    scope* strong_ = nullptr; // Only set on root sopes.
                              // NULL means no strong amalgamtion.
  };

  // Temporary scope. The idea is to be able to create a temporary scope in
  // order not to change the variables in the current scope.  Such a scope is
  // not entered in to the scope map. As a result it can only be used as a
  // temporary set of variables. In particular, defining targets directly in
  // such a scope will surely end up badly. Defining any nested scopes will be
  // as if defining such a scope in the parent (since path() returns parent's
  // path).
  //
  class temp_scope: public scope
  {
  public:
    temp_scope (scope& p)
        : scope (false) // Not global.
    {
      out_path_ = p.out_path_;
      src_path_ = p.src_path_;
      parent_ = &p;
      root_ = p.root_;
      // No need to copy strong_ since we are never root scope.
    }
  };

  // Scope map.
  //
  // Protected by the phase mutex. Note that the scope map is only for paths
  // from the out tree.
  //
  using scope_map_base = butl::dir_path_map<scope>;

  class scope_map: public scope_map_base
  {
  public:
    // Note that we assume the first insertion into the map is always the
    // global scope with empty key.
    //
    iterator
    insert (const dir_path&, bool root = false);

    // Find the most qualified scope that encompasses this path.
    //
    const scope&
    find (const dir_path& d) const
    {
      return const_cast<scope_map*> (this)->find (d);
    }

    const scope&
    find (const path& p) const
    {
      // Natural thing to do here would be to call find (p.directory ()).
      // However, there could be a situation where the passed path is a
      // directory (i.e., the calling code does not know what it is dealing
      // with), so let's use the whole path.
      //
      // In fact, ideally, we should have used path_map instead of
      // dir_path_map to be able to search for both paths without any casting
      // (and copies). But currently we have too much stuff pointing to the
      // key.
      //
      return find (path_cast<dir_path> (p));
    }

    // RW access.
    //
  public:
    scope_map&
    rw () const
    {
      assert (phase == run_phase::load);
      return const_cast<scope_map&> (*this);
    }

    scope_map&
    rw (scope&) const {return const_cast<scope_map&> (*this);}

  private:
    static scope_map instance;

    // Entities that can access bypassing the lock proof.
    //
    friend int main (int, char*[]);
    friend variable_overrides reset (const strings&);

    scope&
    find (const dir_path&);

  public:
    static const scope_map& cinstance; // For var_pool initialization.
  };

  extern const scope_map& scopes;
  extern const scope* global_scope;
}

#include <build2/scope.ixx>

#endif // BUILD2_SCOPE_HXX
