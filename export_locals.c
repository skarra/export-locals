/*
 * export_locals.c - Extract Local Symbols from a ELF file.
 *
 * Sriram Karra <karra@shakti.homelinux.net>
 *
 * Created : Thu Nov 11 17:24:15 2004
 *
 * Copyright (C) 2004, Sriram Karra.  All Rights Reserved.
 *
 * $Id$
 *
 * L I C E N S E
 * = = = = = = =
 * 
 *     This program is distributed under the terms of GPL v2.  Also note
 *     that this depends on libbfd, which is itself GPLed (*not* LGPL),
 *     and as such one doesn't have much choice.
 *
 * Intro
 *
 *     The idea is to implement some amount of binary rewrite capability
 *     for ELF files.  A binary rewrite functionality that is
 *     immediately relevant and necessary is to be able to extract all
 *     static symbols from a object's symbol table and output them alone
 *     into a separate binary; after which those symbols can be linked
 *     to from another file.  Duplicate static symbols should be
 *     resolved using some user-provided, or, alternately, some sensible
 *     default heuristics.
 *
 * Usage
 *
 *     ./export_locals <input_obj_file> -syms a,b,c,d
 *     
 *     Where the symbols a, b, c, d etc. are to be extracted and
 *     exported.  The output elf file `foo'.
 *     
 *     Since we are dealing with symbols having local scope, it is
 *     possile for multiple symbols to be present with the same name.
 *     For now we handle this case by selecting the 'first' symbol we
 *     encounter.  More sophisticated things may be made available in
 *     future.
 * 
 * Status:
 *
 * Nov 27, 2004:
 *
 *     Currently able to export specified local symbols to an output
 *     file.  There is a small problem with the address of the symbols
 *     after the output file is linked with an external file.  The
 *     output file contains the right address, but after linking with
 *     another file, the address of the externally linked symbol
 *     differs.  Need to verify if this is really a problem or not.
 *     Certainly looks like a problem... But let's see.
 *
 * Potential Issues
 *
 *     This has been tested with libbfd-2.15.  It is bound to have
 *     issues with older versions.  bfd_boolean, TRUE and FALSE may not
 *     be defined; bfd internal types could also be different leading to
 *     type mismatch errors.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bfd.h>
#include <assert.h>

static int dummy_local_sym;

static struct parsed_args_ {
    char *inp_filename;
    char *out_filename;
    int  n_syms;		/* No. of symbols requested for
				 * exporting */
    char **sym_names;    
} brew_arg_info;

static int
brew_is_local_symbol (bfd *b, asymbol *a)
{
    flagword f;

    f = a->flags;
    return ((f & BSF_LOCAL) == BSF_LOCAL
	    && !bfd_is_local_label(b, a));
}

static int
brew_get_local_sym_count (bfd *b, asymbol **sym_tab)
{
    int      i, ret;
    long     nsym;
    asymbol  *a;

    bfd_set_error(bfd_error_no_error);

    nsym = bfd_canonicalize_symtab(b, sym_tab);
    if (bfd_get_error()) {
	bfd_perror("could not canonicalize sym tab: ");
    }

    if (!nsym) {
	printf("Right... something hosed.  nsym.\n");
    } else {
	printf("%ld symbols found\n", nsym);
    }
 
    for (i = 0, ret = 0; i < nsym; ++i) {
	a = sym_tab[i];
	if (brew_is_local_symbol(b, a)) {
	    ++ret;
	}
    }

    return (ret);
}

static bfd_boolean
brew_seen_symbol_before (const char **names, const char *name, int *size)
{
    int i;

    for (i = 0; i < *size; ++i) {
	if (strcmp(names[i], name) == 0) {
	    return (TRUE);
	}
    }

    names[(*size)++] = name;
    return (FALSE);
}

/*
 * Returns the number of symbols.  Caller should free *ptrs.
 */
static int
brew_extract_locals_as_globals (bfd *b, bfd *out, asymbol ***ptrs,
				char **syms, int n_syms)
{
    int 	 i, j, nnames;
    long 	 stn, nsym, nlocals, l;
    asymbol 	*a, *new, **sym_tab;
    asection 	*sec;
    bfd_boolean  found;
    const char  **names;
    const char 	*sec_name;

    bfd_set_error(bfd_error_no_error);

    stn     = bfd_get_symtab_upper_bound(b);
    sym_tab = malloc(stn);
    nsym    = bfd_canonicalize_symtab(b, sym_tab);
    names   = malloc(nsym * sizeof(char*));

    if (bfd_get_error()) {
	bfd_perror("could not canonicalize sym tab: ");
    }

    nlocals = brew_get_local_sym_count(b, sym_tab);
    *ptrs   = malloc((nlocals + 1) * sizeof(asymbol*));
     
    for (i = 0, l = 0, nnames = 0; i < nsym; ++i) {
	a = sym_tab[i];

	if (!brew_is_local_symbol(b, a))
	    continue;

	found = FALSE;
	for (j = 0; j < n_syms; ++j) {
	    if (strcmp(a->name, syms[j]) == 0) {
		found = TRUE;
		break;
	    }
	}

	if (!found)
	    continue;

	sec_name = bfd_get_section_name(b, a->section);
	assert(sec_name);
	sec = bfd_get_section_by_name(out, sec_name);
	if (!sec) {
	    printf("No sec. for section name: %s.  "
		   "Skipping symbol '%s'\n", sec_name, a->name);
	    continue;
	}

	if (!brew_seen_symbol_before(names, a->name, &nnames)) {
	    new 	 = bfd_make_empty_symbol(out);
	    new->name    = a->name;
	    new->section = sec;
	    new->flags   = BSF_GLOBAL;
	    new->value   = a->value;
	    (*ptrs)[l++] = new;
	}
    }

    (*ptrs)[l] = 0;
    free(sym_tab);
    free(names);

    return (l);
}

/*
 * In 'out', create all the sections that are available in 'b'
 */
static void
brew_copy_section_names (bfd *b, bfd *out)
{
    asection *s, *o;

    bfd_set_error(bfd_error_no_error);

#if 0
    nsecs = bfd_count_sections(b);
    if (bfd_get_error()) {
	bfd_perror(__FUNCTION__ ": bfd_count_sections failed: ");
	return;
    }
#endif

    for (s = b->sections; s; s = s->next) {
	o = bfd_make_section_old_way(out, bfd_get_section_name(b, s));
	o->user_set_vma = 1;
	o->vma = s->vma;
    }
}

/*
 * This routine walks through the symbol table of given file and
 * produces a new ELF file with those exported as globals
 *
 * syms    - A list if symbol names to export.
 * n_syms  - The number of syms.  If 0, all the local symbols found will
 *           be exported.
 */
static void
brew_export_locals (bfd *b, int n_syms, char **syms, char *out_file)
{
    bfd 	 *out;
    asymbol 	**ptrs;
    long 	  nsyms;

    out = bfd_openw(out_file, "elf32-i386");
    bfd_set_format(out, bfd_object);

    brew_copy_section_names(b, out);
    nsyms = brew_extract_locals_as_globals(b, out, &ptrs, syms, n_syms);

    bfd_set_symtab(out, ptrs, nsyms);
    bfd_close(out);

    free(ptrs);
}

static void
brew_list_matching_formats (char **p)
{
    fprintf(stderr, "brewrite: Matching formats: ");
    while (*p) {
	fprintf(stderr, " %s", *p++);
    }

    fputc('\n', stderr);
}

static void brew_show_sections (bfd*)     __attribute__ ((unused));
static void brew_show_symbols (bfd*)      __attribute__ ((unused));
static void brew_print_target_list (void) __attribute__ ((unused));

static void
brew_show_sections (bfd *b)
{
    int        i, nsecs;
    asection   *s;

    bfd_set_error(bfd_error_no_error);

    nsecs = bfd_count_sections(b);
    printf("No. of sections = %d\n", nsecs);
    if (bfd_get_error()) {
	bfd_perror("count sections failed: ");
    }

    for (s = b->sections, i = 0; s; s = s->next, ++i) {
	printf("Section [%2d] = %s\n", i, bfd_get_section_name(b, s));
    }

    assert(i == nsecs);
}

static void
brew_show_symbols (bfd *b)
{
    int       	 i;
    long      	 stn, nsym = 0;
    asymbol   	*a, **sym_tab;
    asection  	*s;
    flagword  	 f;

    bfd_set_error(bfd_error_no_error);

    // It has been noticed that the the .symtab section is not
    // 'available', but the symbols themselves are available...
    // wierd...
    s = bfd_get_section_by_name(b, ".symtab");
    if (!s) {
	printf(".symtab not found here dude... not sure why\n");
    }

    stn     = bfd_get_symtab_upper_bound(b);
    sym_tab = malloc(stn);
    nsym    = bfd_canonicalize_symtab(b, sym_tab);
    if (bfd_get_error()) {
	bfd_perror("could not canonicalize sym tab: ");
    }

    if (!nsym) {
	printf("Right... something hosed.  nsym.\n");
    } else {
	printf("%ld symbols found\n", nsym);
    }
 
    for (i = 0; i < nsym; ++i) {
	a = sym_tab[i];
	f = a->flags;
	if ((f & BSF_LOCAL) == BSF_LOCAL
	    && !bfd_is_local_label(b, a)) {
	    printf("%-25s %#8llx %s\n",
		   bfd_asymbol_name(a),
		   bfd_asymbol_value(a),
		   bfd_get_section_name(b, bfd_get_section(a)));
	}
    }

    free(sym_tab);
}

static void
print_args (struct parsed_args_ *a)
{
    int i;

    if (a->out_filename) {
	printf("output filename = %s\n", a->out_filename);
    }

    printf("No. of input symbols   = %d\n", a->n_syms);
    printf("Requested symbols = ");
    for (i = 0; i < a->n_syms; ++i) {
	printf("%s ", a->sym_names[i]);
    }
    printf("\n");
}

/*
 * Parses the args and populates the fields of 'a' appropriately.
 */
static void
parse_args (int argc, char *argv[], struct parsed_args_ *a)
{
    int 	 i;
    char 	*s;
    void 	*v;

    a->inp_filename = "a.out";
    a->n_syms 	    = 0;
    a->sym_names    = malloc(sizeof(char**));
    a->sym_names[0] = NULL;

    for (i = 1; i < argc; ++i) {
	if (strcmp(argv[i], "-syms") == 0) {
	    s = strtok(argv[++i], ", ");
	    while (s) {
		v = realloc(a->sym_names, a->n_syms + 1);
		if (!v) {
		    fprintf(stderr, "Out of memeory in %s\n", __FUNCTION__);
		    abort();
		}

		a->sym_names = v;
		a->sym_names[a->n_syms] = malloc(strlen(s) + 1);
		strcpy(a->sym_names[a->n_syms++], s);

		s = strtok(NULL, ", ");
	    }
	} else 	if (strcmp(argv[i], "-out") == 0) {
	    if (i == argc) {
		fprintf(stderr, "-out flag requires output filename\n");
		abort();
	    }

	    a->out_filename = argv[++i];
	} else {
	    a->inp_filename = malloc(strlen(argv[i]) + 1);
	    strcpy(a->inp_filename, argv[i]);
	}
    }
}

int
main (int argc, char *argv[])
{
    bfd    	*b;
    char   	**matching;

    parse_args(argc, argv, &brew_arg_info);
    print_args(&brew_arg_info);

    bfd_init();
    if (bfd_get_error()) {
	bfd_perror("bfd_init failed: ");
    }

    b = bfd_openr(brew_arg_info.inp_filename, NULL);
    if (!b) {
	bfd_perror("bfd_openr failed: ");
	return (1);
    }

    /*
     * The following call is, for as yet unknown reason, quite crucial.
     * Things just do not work otherwise.
     */
    if (!bfd_check_format_matches(b, bfd_object, &matching)) {
	if (bfd_get_error() == bfd_error_file_ambiguously_recognized) {
	    fprintf(stderr,
		    "Ambigous input file.  BFD confused \n");
	    brew_list_matching_formats(matching);
	    free(matching);
	} else {
	    bfd_perror("Wierd error during format check: ");
	}

	return (1);
    }

    brew_export_locals(b, brew_arg_info.n_syms, brew_arg_info.sym_names,
		       brew_arg_info.out_filename);
//     brew_show_symbols(b);
//     brew_show_sections(b);

    bfd_close(b);
    return (dummy_local_sym);
}

/*
 * Take a look at the available target names.  On my Debian machine ath
 * home, this is what I got when I just executed this routine.  One of
 * these strings need to be passed as the second argument for
 * bfd_open.() routines.
 *
 * Target  1 = elf32-i386
 * Target  2 = a.out-i386-linux
 * Target  3 = elf32-little
 * Target  4 = elf32-big
 * Target  5 = srec
 * Target  6 = symbolsrec
 * Target  7 = tekhex
 * Target  8 = binary
 * Target  9 = ihex
 * Target 10 = trad-core
 */
static void
brew_print_target_list (void)
{
    int   i;
    const char **ts;

    ts = bfd_target_list();

    if (ts) {
	i = 0;
	while (*ts) {
	    printf("Target %2d = %s\n", ++i, *ts);
	    ts++;
	}
    } else {
	printf("Null target list.. duh\n");
    }
}
