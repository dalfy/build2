// -*- C++ -*-
//
// This file was generated by CLI, a command line interface
// compiler for C++.
//

// Begin prologue.
//
#include <build2/types-parsers.hxx>
//
// End prologue.

#include <build2/b-options.hxx>

#include <map>
#include <set>
#include <string>
#include <vector>
#include <ostream>
#include <sstream>
#include <cstring>
#include <fstream>

namespace build2
{
  namespace cl
  {
    // unknown_option
    //
    unknown_option::
    ~unknown_option () throw ()
    {
    }

    void unknown_option::
    print (::std::ostream& os) const
    {
      os << "unknown option '" << option ().c_str () << "'";
    }

    const char* unknown_option::
    what () const throw ()
    {
      return "unknown option";
    }

    // unknown_argument
    //
    unknown_argument::
    ~unknown_argument () throw ()
    {
    }

    void unknown_argument::
    print (::std::ostream& os) const
    {
      os << "unknown argument '" << argument ().c_str () << "'";
    }

    const char* unknown_argument::
    what () const throw ()
    {
      return "unknown argument";
    }

    // missing_value
    //
    missing_value::
    ~missing_value () throw ()
    {
    }

    void missing_value::
    print (::std::ostream& os) const
    {
      os << "missing value for option '" << option ().c_str () << "'";
    }

    const char* missing_value::
    what () const throw ()
    {
      return "missing option value";
    }

    // invalid_value
    //
    invalid_value::
    ~invalid_value () throw ()
    {
    }

    void invalid_value::
    print (::std::ostream& os) const
    {
      os << "invalid value '" << value ().c_str () << "' for option '"
         << option ().c_str () << "'";
    }

    const char* invalid_value::
    what () const throw ()
    {
      return "invalid option value";
    }

    // eos_reached
    //
    void eos_reached::
    print (::std::ostream& os) const
    {
      os << what ();
    }

    const char* eos_reached::
    what () const throw ()
    {
      return "end of argument stream reached";
    }

    // file_io_failure
    //
    file_io_failure::
    ~file_io_failure () throw ()
    {
    }

    void file_io_failure::
    print (::std::ostream& os) const
    {
      os << "unable to open file '" << file ().c_str () << "' or read failure";
    }

    const char* file_io_failure::
    what () const throw ()
    {
      return "unable to open file or read failure";
    }

    // unmatched_quote
    //
    unmatched_quote::
    ~unmatched_quote () throw ()
    {
    }

    void unmatched_quote::
    print (::std::ostream& os) const
    {
      os << "unmatched quote in argument '" << argument ().c_str () << "'";
    }

    const char* unmatched_quote::
    what () const throw ()
    {
      return "unmatched quote";
    }

    // scanner
    //
    scanner::
    ~scanner ()
    {
    }

    // argv_scanner
    //
    bool argv_scanner::
    more ()
    {
      return i_ < argc_;
    }

    const char* argv_scanner::
    peek ()
    {
      if (i_ < argc_)
        return argv_[i_];
      else
        throw eos_reached ();
    }

    const char* argv_scanner::
    next ()
    {
      if (i_ < argc_)
      {
        const char* r (argv_[i_]);

        if (erase_)
        {
          for (int i (i_ + 1); i < argc_; ++i)
            argv_[i - 1] = argv_[i];

          --argc_;
          argv_[argc_] = 0;
        }
        else
          ++i_;

        return r;
      }
      else
        throw eos_reached ();
    }

    void argv_scanner::
    skip ()
    {
      if (i_ < argc_)
        ++i_;
      else
        throw eos_reached ();
    }

    // argv_file_scanner
    //
    bool argv_file_scanner::
    more ()
    {
      if (!args_.empty ())
        return true;

      while (base::more ())
      {
        // See if the next argument is the file option.
        //
        const char* a (base::peek ());
        const option_info* oi;

        if (!skip_ && (oi = find (a)))
        {
          base::next ();

          if (!base::more ())
            throw missing_value (oi->option);

          if (oi->search_func != 0)
          {
            std::string f (oi->search_func (base::next (), oi->arg));

            if (!f.empty ())
              load (f);
          }
          else
            load (base::next ());

          if (!args_.empty ())
            return true;
        }
        else
        {
          if (!skip_)
            skip_ = (std::strcmp (a, "--") == 0);

          return true;
        }
      }

      return false;
    }

    const char* argv_file_scanner::
    peek ()
    {
      if (!more ())
        throw eos_reached ();

      return args_.empty () ? base::peek () : args_.front ().c_str ();
    }

    const char* argv_file_scanner::
    next ()
    {
      if (!more ())
        throw eos_reached ();

      if (args_.empty ())
        return base::next ();
      else
      {
        hold_[i_ == 0 ? ++i_ : --i_].swap (args_.front ());
        args_.pop_front ();
        return hold_[i_].c_str ();
      }
    }

    void argv_file_scanner::
    skip ()
    {
      if (!more ())
        throw eos_reached ();

      if (args_.empty ())
        return base::skip ();
      else
        args_.pop_front ();
    }

    const argv_file_scanner::option_info* argv_file_scanner::
    find (const char* a) const
    {
      for (std::size_t i (0); i < options_count_; ++i)
        if (std::strcmp (a, options_[i].option) == 0)
          return &options_[i];

      return 0;
    }

    void argv_file_scanner::
    load (const std::string& file)
    {
      using namespace std;

      ifstream is (file.c_str ());

      if (!is.is_open ())
        throw file_io_failure (file);

      while (!is.eof ())
      {
        string line;
        getline (is, line);

        if (is.fail () && !is.eof ())
          throw file_io_failure (file);

        string::size_type n (line.size ());

        // Trim the line from leading and trailing whitespaces.
        //
        if (n != 0)
        {
          const char* f (line.c_str ());
          const char* l (f + n);

          const char* of (f);
          while (f < l && (*f == ' ' || *f == '\t' || *f == '\r'))
            ++f;

          --l;

          const char* ol (l);
          while (l > f && (*l == ' ' || *l == '\t' || *l == '\r'))
            --l;

          if (f != of || l != ol)
            line = f <= l ? string (f, l - f + 1) : string ();
        }

        // Ignore empty lines, those that start with #.
        //
        if (line.empty () || line[0] == '#')
          continue;

        string::size_type p (line.find (' '));

        if (p == string::npos)
        {
          if (!skip_)
            skip_ = (line == "--");

          args_.push_back (line);
        }
        else
        {
          string s1 (line, 0, p);

          // Skip leading whitespaces in the argument.
          //
          n = line.size ();
          for (++p; p < n; ++p)
          {
            char c (line[p]);

            if (c != ' ' && c != '\t' && c != '\r')
              break;
          }

          string s2 (line, p);

          // If the string is wrapped in quotes, remove them.
          //
          n = s2.size ();
          char cf (s2[0]), cl (s2[n - 1]);

          if (cf == '"' || cf == '\'' || cl == '"' || cl == '\'')
          {
            if (n == 1 || cf != cl)
              throw unmatched_quote (s2);

            s2 = string (s2, 1, n - 2);
          }

          const option_info* oi;
          if (!skip_ && (oi = find (s1.c_str ())))
          {
            if (s2.empty ())
              throw missing_value (oi->option);

            if (oi->search_func != 0)
            {
              std::string f (oi->search_func (s2.c_str (), oi->arg));

              if (!f.empty ())
                load (f);
            }
            else
              load (s2);
          }
          else
          {
            args_.push_back (s1);
            args_.push_back (s2);
          }
        }
      }
    }

    template <typename X>
    struct parser
    {
      static void
      parse (X& x, bool& xs, scanner& s)
      {
        using namespace std;

        const char* o (s.next ());

        if (s.more ())
        {
          string v (s.next ());
          istringstream is (v);
          if (!(is >> x && is.peek () == istringstream::traits_type::eof ()))
            throw invalid_value (o, v);
        }
        else
          throw missing_value (o);

        xs = true;
      }
    };

    template <>
    struct parser<bool>
    {
      static void
      parse (bool& x, scanner& s)
      {
        s.next ();
        x = true;
      }
    };

    template <>
    struct parser<std::string>
    {
      static void
      parse (std::string& x, bool& xs, scanner& s)
      {
        const char* o (s.next ());

        if (s.more ())
          x = s.next ();
        else
          throw missing_value (o);

        xs = true;
      }
    };

    template <typename X>
    struct parser<std::vector<X> >
    {
      static void
      parse (std::vector<X>& c, bool& xs, scanner& s)
      {
        X x;
        bool dummy;
        parser<X>::parse (x, dummy, s);
        c.push_back (x);
        xs = true;
      }
    };

    template <typename X>
    struct parser<std::set<X> >
    {
      static void
      parse (std::set<X>& c, bool& xs, scanner& s)
      {
        X x;
        bool dummy;
        parser<X>::parse (x, dummy, s);
        c.insert (x);
        xs = true;
      }
    };

    template <typename K, typename V>
    struct parser<std::map<K, V> >
    {
      static void
      parse (std::map<K, V>& m, bool& xs, scanner& s)
      {
        const char* o (s.next ());

        if (s.more ())
        {
          std::string ov (s.next ());
          std::string::size_type p = ov.find ('=');

          K k = K ();
          V v = V ();
          std::string kstr (ov, 0, p);
          std::string vstr (ov, (p != std::string::npos ? p + 1 : ov.size ()));

          int ac (2);
          char* av[] = 
          {
            const_cast<char*> (o), 0
          };

          bool dummy;
          if (!kstr.empty ())
          {
            av[1] = const_cast<char*> (kstr.c_str ());
            argv_scanner s (0, ac, av);
            parser<K>::parse (k, dummy, s);
          }

          if (!vstr.empty ())
          {
            av[1] = const_cast<char*> (vstr.c_str ());
            argv_scanner s (0, ac, av);
            parser<V>::parse (v, dummy, s);
          }

          m[k] = v;
        }
        else
          throw missing_value (o);

        xs = true;
      }
    };

    template <typename X, typename T, T X::*M>
    void
    thunk (X& x, scanner& s)
    {
      parser<T>::parse (x.*M, s);
    }

    template <typename X, typename T, T X::*M, bool X::*S>
    void
    thunk (X& x, scanner& s)
    {
      parser<T>::parse (x.*M, x.*S, s);
    }
  }
}

#include <map>
#include <cstring>

namespace build2
{
  // options
  //

  options::
  options ()
  : v_ (),
    V_ (),
    progress_ (),
    no_progress_ (),
    quiet_ (),
    verbose_ (1),
    verbose_specified_ (false),
    jobs_ (),
    jobs_specified_ (false),
    max_jobs_ (),
    max_jobs_specified_ (false),
    queue_depth_ (4),
    queue_depth_specified_ (false),
    max_stack_ (),
    max_stack_specified_ (false),
    serial_stop_ (),
    structured_result_ (),
    match_only_ (),
    no_column_ (),
    no_line_ (),
    buildfile_ ("buildfile"),
    buildfile_specified_ (false),
    config_guess_ (),
    config_guess_specified_ (false),
    config_sub_ (),
    config_sub_specified_ (false),
    pager_ (),
    pager_specified_ (false),
    pager_option_ (),
    pager_option_specified_ (false),
    help_ (),
    version_ ()
  {
  }

  bool options::
  parse (int& argc,
         char** argv,
         bool erase,
         ::build2::cl::unknown_mode opt,
         ::build2::cl::unknown_mode arg)
  {
    ::build2::cl::argv_scanner s (argc, argv, erase);
    bool r = _parse (s, opt, arg);
    return r;
  }

  bool options::
  parse (int start,
         int& argc,
         char** argv,
         bool erase,
         ::build2::cl::unknown_mode opt,
         ::build2::cl::unknown_mode arg)
  {
    ::build2::cl::argv_scanner s (start, argc, argv, erase);
    bool r = _parse (s, opt, arg);
    return r;
  }

  bool options::
  parse (int& argc,
         char** argv,
         int& end,
         bool erase,
         ::build2::cl::unknown_mode opt,
         ::build2::cl::unknown_mode arg)
  {
    ::build2::cl::argv_scanner s (argc, argv, erase);
    bool r = _parse (s, opt, arg);
    end = s.end ();
    return r;
  }

  bool options::
  parse (int start,
         int& argc,
         char** argv,
         int& end,
         bool erase,
         ::build2::cl::unknown_mode opt,
         ::build2::cl::unknown_mode arg)
  {
    ::build2::cl::argv_scanner s (start, argc, argv, erase);
    bool r = _parse (s, opt, arg);
    end = s.end ();
    return r;
  }

  bool options::
  parse (::build2::cl::scanner& s,
         ::build2::cl::unknown_mode opt,
         ::build2::cl::unknown_mode arg)
  {
    bool r = _parse (s, opt, arg);
    return r;
  }

  ::build2::cl::usage_para options::
  print_usage (::std::ostream& os, ::build2::cl::usage_para p)
  {
    CLI_POTENTIALLY_UNUSED (os);

    if (p != ::build2::cl::usage_para::none)
      os << ::std::endl;

    os << "\033[1mOPTIONS\033[0m" << ::std::endl;

    os << std::endl
       << "\033[1m-v\033[0m                   Print actual commands being executed. This is equivalent" << ::std::endl
       << "                     to \033[1m--verbose 2\033[0m." << ::std::endl;

    os << std::endl
       << "\033[1m-V\033[0m                   Print all underlying commands being executed. This is" << ::std::endl
       << "                     equivalent to \033[1m--verbose 3\033[0m." << ::std::endl;

    os << std::endl
       << "\033[1m--progress\033[0m|\033[1m-p\033[0m        Display build progress. If printing to a terminal the" << ::std::endl
       << "                     progress is displayed by default for low verbosity levels." << ::std::endl
       << "                     Use \033[1m--no-progress\033[0m to suppress." << ::std::endl;

    os << std::endl
       << "\033[1m--no-progress\033[0m        Don't display build progress." << ::std::endl;

    os << std::endl
       << "\033[1m--quiet\033[0m|\033[1m-q\033[0m           Run quietly, only printing error messages. This is" << ::std::endl
       << "                     equivalent to \033[1m--verbose 0\033[0m." << ::std::endl;

    os << std::endl
       << "\033[1m--verbose\033[0m \033[4mlevel\033[0m      Set the diagnostics verbosity to \033[4mlevel\033[0m between 0 and 6." << ::std::endl
       << "                     Level 0 disables any non-error messages while level 6" << ::std::endl
       << "                     produces lots of information, with level 1 being the" << ::std::endl
       << "                     default. The following additional types of diagnostics are" << ::std::endl
       << "                     produced at each level:" << ::std::endl
       << ::std::endl
       << "                     1. High-level information messages." << ::std::endl
       << "                     2. Essential underlying commands being executed." << ::std::endl
       << "                     3. All underlying commands being executed." << ::std::endl
       << "                     4. Information that could be helpful to the user." << ::std::endl
       << "                     5. Information that could be helpful to the developer." << ::std::endl
       << "                     6. Even more detailed information, including state dumps." << ::std::endl;

    os << std::endl
       << "\033[1m--jobs\033[0m|\033[1m-j\033[0m \033[4mnum\033[0m        Number of active jobs to perform in parallel. This" << ::std::endl
       << "                     includes both the number of active threads inside the" << ::std::endl
       << "                     build system as well as the number of external commands" << ::std::endl
       << "                     (compilers, linkers, etc) started but not yet finished. If" << ::std::endl
       << "                     this option is not specified or specified with the 0\033[0m" << ::std::endl
       << "                     value, then the number of available hardware threads is" << ::std::endl
       << "                     used." << ::std::endl;

    os << std::endl
       << "\033[1m--max-jobs\033[0m|\033[1m-J\033[0m \033[4mnum\033[0m    Maximum number of jobs (threads) to create. The default is" << ::std::endl
       << "                     8x the number of active jobs (--jobs|j\033[0m) on 32-bit" << ::std::endl
       << "                     architectures and 32x on 64-bit. See the build system" << ::std::endl
       << "                     scheduler implementation for details." << ::std::endl;

    os << std::endl
       << "\033[1m--queue-depth\033[0m|\033[1m-Q\033[0m \033[4mnum\033[0m The queue depth as a multiplier over the number of active" << ::std::endl
       << "                     jobs. Normally we want a deeper queue if the jobs take" << ::std::endl
       << "                     long (for example, compilation) and shorter if they are" << ::std::endl
       << "                     quick (for example, simple tests). The default is 4. See" << ::std::endl
       << "                     the build system scheduler implementation for details." << ::std::endl;

    os << std::endl
       << "\033[1m--max-stack\033[0m \033[4mnum\033[0m      The maximum stack size in KBytes to allow for newly" << ::std::endl
       << "                     created threads. For \033[4mpthreads\033[0m-based systems the driver" << ::std::endl
       << "                     queries the stack size of the main thread and uses the" << ::std::endl
       << "                     same size for creating additional threads. This allows" << ::std::endl
       << "                     adjusting the stack size using familiar mechanisms, such" << ::std::endl
       << "                     as \033[1mulimit\033[0m. Sometimes, however, the stack size of the main" << ::std::endl
       << "                     thread is excessively large. As a result, the driver" << ::std::endl
       << "                     checks if it is greater than a predefined limit (64MB on" << ::std::endl
       << "                     64-bit systems and 32MB on 32-bit ones) and caps it to a" << ::std::endl
       << "                     more sensible value (8MB) if that's the case. This option" << ::std::endl
       << "                     allows you to override this check with the special zero" << ::std::endl
       << "                     value indicating that the main thread stack size should be" << ::std::endl
       << "                     used as is." << ::std::endl;

    os << std::endl
       << "\033[1m--serial-stop\033[0m|\033[1m-s\033[0m     Run serially and stop at the first error. This mode is" << ::std::endl
       << "                     useful to investigate build failures that are caused by" << ::std::endl
       << "                     build system errors rather than compilation errors. Note" << ::std::endl
       << "                     that if you don't want to keep going but still want" << ::std::endl
       << "                     parallel execution, add --jobs|-j\033[0m (for example -j 0\033[0m for" << ::std::endl
       << "                     default concurrency)." << ::std::endl;

    os << std::endl
       << "\033[1m--structured-result\033[0m  Write the result of execution in a structured form. In" << ::std::endl
       << "                     this mode, instead of printing to \033[1mSTDERR\033[0m diagnostics" << ::std::endl
       << "                     messages about the outcome of executing actions on" << ::std::endl
       << "                     targets, the driver writes to \033[1mSTDOUT\033[0m a structured result" << ::std::endl
       << "                     description one line per the buildspec action/target pair." << ::std::endl
       << "                     Each line has the following format:" << ::std::endl
       << ::std::endl
       << "                     \033[4mstate\033[0m \033[4mmeta-operation\033[0m \033[4moperation\033[0m \033[4mtarget\033[0m\033[0m" << ::std::endl
       << ::std::endl
       << "                     Where \033[4mstate\033[0m can be one of \033[1munchanged\033[0m, \033[1mchanged\033[0m, or \033[1mfailed\033[0m." << ::std::endl
       << "                     If the action is a pre or post operation, then the outer" << ::std::endl
       << "                     operation is specified in parenthesis. For example:" << ::std::endl
       << ::std::endl
       << "                     unchanged perform update(test) /tmp/dir{hello/}" << ::std::endl
       << "                     changed perform test /tmp/dir{hello/}" << ::std::endl
       << ::std::endl
       << "                     Currently only the \033[1mperform\033[0m meta-operation supports the" << ::std::endl
       << "                     structured result output." << ::std::endl;

    os << std::endl
       << "\033[1m--match-only\033[0m         Match the rules but do not execute the operation. This" << ::std::endl
       << "                     mode is primarily useful for profiling." << ::std::endl;

    os << std::endl
       << "\033[1m--no-column\033[0m          Don't print column numbers in diagnostics." << ::std::endl;

    os << std::endl
       << "\033[1m--no-line\033[0m            Don't print line and column numbers in diagnostics." << ::std::endl;

    os << std::endl
       << "\033[1m--buildfile\033[0m \033[4mpath\033[0m     The alternative file to read build information from. The" << ::std::endl
       << "                     default is \033[1mbuildfile\033[0m. If \033[4mpath\033[0m is '\033[1m-\033[0m', then read from" << ::std::endl
       << "                     \033[1mSTDIN\033[0m. Note that this option only affects the files read" << ::std::endl
       << "                     as part of the buildspec processing. Specifically, it has" << ::std::endl
       << "                     no effect on the \033[1msource\033[0m and \033[1minclude\033[0m directives. As a" << ::std::endl
       << "                     result, this option is primarily intended for testing" << ::std::endl
       << "                     rather than changing the build file names in real" << ::std::endl
       << "                     projects." << ::std::endl;

    os << std::endl
       << "\033[1m--config-guess\033[0m \033[4mpath\033[0m  The path to the \033[1mconfig.guess(1)\033[0m script that should be used" << ::std::endl
       << "                     to guess the host machine triplet. If this option is not" << ::std::endl
       << "                     specified, then \033[1mb\033[0m will fall back on to using the target it" << ::std::endl
       << "                     was built for as host." << ::std::endl;

    os << std::endl
       << "\033[1m--config-sub\033[0m \033[4mpath\033[0m    The path to the \033[1mconfig.sub(1)\033[0m script that should be used" << ::std::endl
       << "                     to canonicalize machine triplets. If this option is not" << ::std::endl
       << "                     specified, then \033[1mb\033[0m will use its built-in canonicalization" << ::std::endl
       << "                     support which should be sufficient for commonly-used" << ::std::endl
       << "                     platforms." << ::std::endl;

    os << std::endl
       << "\033[1m--pager\033[0m \033[4mpath\033[0m         The pager program to be used to show long text. Commonly" << ::std::endl
       << "                     used pager programs are \033[1mless\033[0m and \033[1mmore\033[0m. You can also" << ::std::endl
       << "                     specify additional options that should be passed to the" << ::std::endl
       << "                     pager program with \033[1m--pager-option\033[0m. If an empty string is" << ::std::endl
       << "                     specified as the pager program, then no pager will be" << ::std::endl
       << "                     used. If the pager program is not explicitly specified," << ::std::endl
       << "                     then \033[1mb\033[0m will try to use \033[1mless\033[0m. If it is not available, then" << ::std::endl
       << "                     no pager will be used." << ::std::endl;

    os << std::endl
       << "\033[1m--pager-option\033[0m \033[4mopt\033[0m   Additional option to be passed to the pager program. See" << ::std::endl
       << "                     \033[1m--pager\033[0m for more information on the pager program. Repeat" << ::std::endl
       << "                     this option to specify multiple pager options." << ::std::endl;

    os << std::endl
       << "\033[1m--help\033[0m               Print usage information and exit." << ::std::endl;

    os << std::endl
       << "\033[1m--version\033[0m            Print version and exit." << ::std::endl;

    p = ::build2::cl::usage_para::option;

    return p;
  }

  typedef
  std::map<std::string, void (*) (options&, ::build2::cl::scanner&)>
  _cli_options_map;

  static _cli_options_map _cli_options_map_;

  struct _cli_options_map_init
  {
    _cli_options_map_init ()
    {
      _cli_options_map_["-v"] = 
      &::build2::cl::thunk< options, bool, &options::v_ >;
      _cli_options_map_["-V"] = 
      &::build2::cl::thunk< options, bool, &options::V_ >;
      _cli_options_map_["--progress"] = 
      &::build2::cl::thunk< options, bool, &options::progress_ >;
      _cli_options_map_["-p"] = 
      &::build2::cl::thunk< options, bool, &options::progress_ >;
      _cli_options_map_["--no-progress"] = 
      &::build2::cl::thunk< options, bool, &options::no_progress_ >;
      _cli_options_map_["--quiet"] = 
      &::build2::cl::thunk< options, bool, &options::quiet_ >;
      _cli_options_map_["-q"] = 
      &::build2::cl::thunk< options, bool, &options::quiet_ >;
      _cli_options_map_["--verbose"] = 
      &::build2::cl::thunk< options, uint16_t, &options::verbose_,
        &options::verbose_specified_ >;
      _cli_options_map_["--jobs"] = 
      &::build2::cl::thunk< options, size_t, &options::jobs_,
        &options::jobs_specified_ >;
      _cli_options_map_["-j"] = 
      &::build2::cl::thunk< options, size_t, &options::jobs_,
        &options::jobs_specified_ >;
      _cli_options_map_["--max-jobs"] = 
      &::build2::cl::thunk< options, size_t, &options::max_jobs_,
        &options::max_jobs_specified_ >;
      _cli_options_map_["-J"] = 
      &::build2::cl::thunk< options, size_t, &options::max_jobs_,
        &options::max_jobs_specified_ >;
      _cli_options_map_["--queue-depth"] = 
      &::build2::cl::thunk< options, size_t, &options::queue_depth_,
        &options::queue_depth_specified_ >;
      _cli_options_map_["-Q"] = 
      &::build2::cl::thunk< options, size_t, &options::queue_depth_,
        &options::queue_depth_specified_ >;
      _cli_options_map_["--max-stack"] = 
      &::build2::cl::thunk< options, size_t, &options::max_stack_,
        &options::max_stack_specified_ >;
      _cli_options_map_["--serial-stop"] = 
      &::build2::cl::thunk< options, bool, &options::serial_stop_ >;
      _cli_options_map_["-s"] = 
      &::build2::cl::thunk< options, bool, &options::serial_stop_ >;
      _cli_options_map_["--structured-result"] = 
      &::build2::cl::thunk< options, bool, &options::structured_result_ >;
      _cli_options_map_["--match-only"] = 
      &::build2::cl::thunk< options, bool, &options::match_only_ >;
      _cli_options_map_["--no-column"] = 
      &::build2::cl::thunk< options, bool, &options::no_column_ >;
      _cli_options_map_["--no-line"] = 
      &::build2::cl::thunk< options, bool, &options::no_line_ >;
      _cli_options_map_["--buildfile"] = 
      &::build2::cl::thunk< options, path, &options::buildfile_,
        &options::buildfile_specified_ >;
      _cli_options_map_["--config-guess"] = 
      &::build2::cl::thunk< options, path, &options::config_guess_,
        &options::config_guess_specified_ >;
      _cli_options_map_["--config-sub"] = 
      &::build2::cl::thunk< options, path, &options::config_sub_,
        &options::config_sub_specified_ >;
      _cli_options_map_["--pager"] = 
      &::build2::cl::thunk< options, string, &options::pager_,
        &options::pager_specified_ >;
      _cli_options_map_["--pager-option"] = 
      &::build2::cl::thunk< options, strings, &options::pager_option_,
        &options::pager_option_specified_ >;
      _cli_options_map_["--help"] = 
      &::build2::cl::thunk< options, bool, &options::help_ >;
      _cli_options_map_["--version"] = 
      &::build2::cl::thunk< options, bool, &options::version_ >;
    }
  };

  static _cli_options_map_init _cli_options_map_init_;

  bool options::
  _parse (const char* o, ::build2::cl::scanner& s)
  {
    _cli_options_map::const_iterator i (_cli_options_map_.find (o));

    if (i != _cli_options_map_.end ())
    {
      (*(i->second)) (*this, s);
      return true;
    }

    return false;
  }

  bool options::
  _parse (::build2::cl::scanner& s,
          ::build2::cl::unknown_mode opt_mode,
          ::build2::cl::unknown_mode arg_mode)
  {
    bool r = false;
    bool opt = true;

    while (s.more ())
    {
      const char* o = s.peek ();

      if (std::strcmp (o, "--") == 0)
      {
        opt = false;
      }

      if (opt && _parse (o, s))
        r = true;
      else if (opt && std::strncmp (o, "-", 1) == 0 && o[1] != '\0')
      {
        switch (opt_mode)
        {
          case ::build2::cl::unknown_mode::skip:
          {
            s.skip ();
            r = true;
            continue;
          }
          case ::build2::cl::unknown_mode::stop:
          {
            break;
          }
          case ::build2::cl::unknown_mode::fail:
          {
            throw ::build2::cl::unknown_option (o);
          }
        }

        break;
      }
      else
      {
        switch (arg_mode)
        {
          case ::build2::cl::unknown_mode::skip:
          {
            s.skip ();
            r = true;
            continue;
          }
          case ::build2::cl::unknown_mode::stop:
          {
            break;
          }
          case ::build2::cl::unknown_mode::fail:
          {
            throw ::build2::cl::unknown_argument (o);
          }
        }

        break;
      }
    }

    return r;
  }
}

namespace build2
{
  ::build2::cl::usage_para
  print_b_usage (::std::ostream& os, ::build2::cl::usage_para p)
  {
    CLI_POTENTIALLY_UNUSED (os);

    if (p != ::build2::cl::usage_para::none)
      os << ::std::endl;

    os << "\033[1mSYNOPSIS\033[0m" << ::std::endl
       << ::std::endl
       << "\033[1mb --help\033[0m" << ::std::endl
       << "\033[1mb --version\033[0m" << ::std::endl
       << "\033[1mb\033[0m [\033[4moptions\033[0m] [\033[4mvariables\033[0m] [\033[4mbuild-spec\033[0m]\033[0m" << ::std::endl
       << ::std::endl
       << "\033[4mbuild-spec\033[0m = \033[4mmeta-operation\033[0m\033[1m(\033[0m\033[4moperation\033[0m\033[1m(\033[0m\033[4mtarget\033[0m...[\033[1m,\033[0m\033[4mparameters\033[0m]\033[1m)\033[0m...\033[1m)\033[0m...\033[0m" << ::std::endl
       << ::std::endl
       << "\033[1mDESCRIPTION\033[0m" << ::std::endl
       << ::std::endl
       << "The \033[1mbuild2\033[0m driver executes a set of meta-operations on operations on targets" << ::std::endl
       << "according to the build specification, or \033[4mbuildspec\033[0m for short. This process can" << ::std::endl
       << "be controlled by specifying driver \033[4moptions\033[0m and build system \033[4mvariables\033[0m." << ::std::endl
       << ::std::endl
       << "Note that \033[4moptions\033[0m, \033[4mvariables\033[0m, and \033[4mbuild-spec\033[0m fragments can be specified in any" << ::std::endl
       << "order. To avoid treating an argument that starts with \033[1m'-'\033[0m as an option, add the" << ::std::endl
       << "\033[1m'--'\033[0m separator. To avoid treating an argument that contains \033[1m'='\033[0m as a variable," << ::std::endl
       << "add the second \033[1m'--'\033[0m separator." << ::std::endl;

    p = ::build2::options::print_usage (os, ::build2::cl::usage_para::text);

    if (p != ::build2::cl::usage_para::none)
      os << ::std::endl;

    os << "\033[1mEXIT STATUS\033[0m" << ::std::endl
       << ::std::endl
       << "Non-zero exit status is returned in case of an error." << ::std::endl;

    os << std::endl
       << "\033[1mENVIRONMENT\033[0m" << ::std::endl
       << ::std::endl
       << "The \033[1mHOME\033[0m environment variable is used to determine the user's home directory." << ::std::endl
       << "If it is not set, then \033[1mgetpwuid(3)\033[0m is used instead. This value is used to" << ::std::endl
       << "shorten paths printed in diagnostics by replacing the home directory with \033[1m~/\033[0m." << ::std::endl
       << "It is also made available to \033[1mbuildfile\033[0m's as the \033[1mbuild.home\033[0m variable." << ::std::endl;

    p = ::build2::cl::usage_para::text;

    return p;
  }
}

// Begin epilogue.
//
//
// End epilogue.

