/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * GMAMEUI
 *
 * Copyright 2008-2010 Andrew Burton <adb@iinet.net.au>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>
 *
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>      /* For isdigit */

#include "mame-exec.h"
#include "common.h"
#include "io.h"		/* For my_dtostr */

#define BUFFER_SIZE 1000

const gchar *noloadconfig_option_name [] = {
	"noloadconfig",
	"noreadconfig"
};

const gchar *showusage_option_name [] = {
	"help",
	"showusage"
};

G_DEFINE_TYPE (MameExec, mame_exec, G_TYPE_OBJECT)

struct _MameExecPrivate {
	gchar *path;	/* Full path to the executable */
	gchar *name;
	gchar *target;
	gchar *version;

	ExecutableType type;

	const gchar *noloadconfig_option;
	const gchar *showusage_option;

	GHashTable *supported_options;	/* Hash table of command-line options supported by this version */
};

static void mame_exec_class_init (MameExecClass *klass);
static void mame_exec_init (MameExec *exec);
static void mame_exec_finalize (GObject *obj);
static void mame_exec_set_property (GObject *object,
					 guint prop_id,
					 const GValue *value,
					 GParamSpec *pspec);
static void mame_exec_get_property (GObject *object,
					 guint prop_id,
					 GValue *value,
					 GParamSpec *pspec);

static void
mame_exec_set_exectype (MameExec *exec, ExecutableType type);

static gboolean
mame_exec_set_version (MameExec *exec);

static void
mame_opt_free (gpointer opt);

/**
* Compares the version of this executable with the given one.
*
* Returns:
*	 <0 this executable < version
*	  0 this executable == version
*	 >0 this executable > version
*/
static int
mame_compare_version (const MameExec *exec, const gchar *version);

/* Counts the number of possible values for this option. 
* On error it returns -1.
*/
int 
mame_get_option_value_count (const MameExec *exec, const gchar *option_name);

#ifdef ENABLE_XMAME_SVGALIB_SUPPORT
/* version detection for xmame.svgalib */
static gboolean
xmamesvga_executable_set_version (XmameExecutable *exec)
{
	FILE *xmame_pipe;
	FILE *tmp;
	gchar *opt, *p;
	gchar *tmp_file;
	gchar line[BUFFER_SIZE];
	gchar *tmp_xmame_target;
	gchar *tmp_xmame_version;
	gboolean xmame_ok;

	/* FIXME: Generate true tmp name */
	tmp_file = g_strdup ("/tmp/gmameui.12344");
	
	GMAMEUI_DEBUG ("Executing %s with tmp_file \"%s\"", exec->path, tmp_file);
	opt = g_strdup_printf ("%s -version -noloadconfig -out %s 2>/dev/null", exec->path, tmp_file);
	xmame_pipe = popen (opt, "r");
	g_free (opt);
	if (!xmame_pipe)
		goto exit_with_error;

	while (fgets (line, BUFFER_SIZE, xmame_pipe));
	pclose (xmame_pipe);
	
	xmame_ok = FALSE;
	tmp = fopen (tmp_file, "r");
	if (tmp) {
		while (fgets (line, BUFFER_SIZE, tmp)) {
			/* that should catch any info displayed before the version name */
			if (!strncmp (line, "info", 4) )
				continue;
	
			/* try to find the '(' */
			for (p = line; (*p && (*p != '(')); p++);
			if (!*p)
				continue;
			* (p - 1) = '\0';
			p++;
			tmp_xmame_target = p;

			/* now try to find the ')' */
			for (; (*p && (*p != ')')); p++);
			if (!*p)
				continue;
			*p = '\0';
			p += 2;
			tmp_xmame_version = p;
		
			/* do I need that ? */
			for (; (*p && (*p != '\n')); p++);
			*p = '\0';
			if (strncmp (tmp_xmame_version, "version", 7))
				continue;
			
			GMAMEUI_DEBUG ("checking xmame version: %s (%s) %s", line, tmp_xmame_target, tmp_xmame_version);
			
			exec->name = g_strdup (line);
			exec->target = g_strdup (tmp_xmame_target);
			exec->version = g_strdup (tmp_xmame_version);
			
			if (!strcmp (exec->target, "x11"))
				exec->type = XMAME_EXEC_X11;
			else if (!strcmp (exec->target, "svgalib"))
				exec->type = XMAME_EXEC_SVGALIB;
			else if (!strcmp (exec->target, "ggi"))
				exec->type = XMAME_EXEC_GGI;
			else if (!strcmp (exec->target, "xgl"))
				exec->type = XMAME_EXEC_XGL;
			else if (!strcmp (exec->target, "xfx"))
				exec->type = XMAME_EXEC_XFX;
			else if (!strcmp (exec->target, "svgafx"))
				exec->type = XMAME_EXEC_SVGAFX;
			else if (!strcmp (exec->target, "SDL"))
				exec->type = XMAME_EXEC_SDL;
			else if (!strcmp (exec->target, "photon2"))
				exec->type = XMAME_EXEC_PHOTON2;
			else
				exec->type = XMAME_EXEC_UNKNOWN;

			xmame_ok = TRUE;
		}
		fclose (tmp);
		unlink (tmp_file);
		g_free (tmp_file);
	}

	return xmame_ok;
exit_with_error:
	GMAMEUI_DEBUG ("Could not execute.");
	unlink (tmp_file);
	g_free (tmp_file);
	return FALSE;
}
#endif

/*#ifdef ENABLE_WINMAME_SUPPORT*/
/**
* version detection for win32 mame or SDLMame.
*
*/
static gboolean
winmame_executable_set_version (MameExec *exec)
{
	FILE *xmame_pipe;
	gchar *opt, *p;
	gchar line[BUFFER_SIZE];
	gchar *tmp_xmame_version;
	gboolean xmame_ok = FALSE;

	exec->priv->noloadconfig_option = noloadconfig_option_name[1];
	exec->priv->showusage_option = showusage_option_name[1];

	opt = g_strdup_printf ("%s -help -noreadconfig 2>/dev/null", mame_exec_get_path (exec));
	GMAMEUI_DEBUG ("Trying %s", opt);
	xmame_pipe = popen (opt, "r");
	g_free (opt);
	if (!xmame_pipe)
	{
		GMAMEUI_DEBUG ("Could not execute.");
		return FALSE;
	}

	while (fgets (line, BUFFER_SIZE, xmame_pipe) && !xmame_ok)
	{
		GMAMEUI_DEBUG ("Reading line %s", line);

		/* that should catch any info displayed before the version name */
		if (!strncmp (line, "info", 4) )
			continue;

		/* try to find the second word v.??? (???) */
		for (p = line; (*p && (*p != ' '));p++);
		if (!*p)
			continue;
		* (p-1) = '\0';
		p++;
		tmp_xmame_version = p;	

		/* Stop at ')' */
		for (; (*p && (*p != ')'));p++);
		if (!*p)
			continue;

		if (* (p+1))
			* (p+1) = '\0';
						
		GMAMEUI_DEBUG ("checking xmame version: %s (Win32) %s", line, tmp_xmame_version);
		
		g_object_set (exec,
			      "exec-name", line,
			      "exec-target", "Win32",
			      "exec-version", tmp_xmame_version,
			      NULL);

		xmame_ok = TRUE;
	}
		
	pclose (xmame_pipe);
		
	if (xmame_ok)
		mame_exec_set_exectype (exec, XMAME_EXEC_WIN32);

	return xmame_ok;
}
/*#endif*/

/* Sets the version and type information for the executable.
* This is called automatically for every executable
* added to the table so there is no need to call it.
*
* Note: This is called before parsing the options
* so we cannot use
*/
/* FIXME: Possible memory leak... */
static gboolean
mame_exec_set_version (MameExec *exec)
{
	FILE *xmame_pipe;
	gchar *opt, *p;
	gchar line[BUFFER_SIZE];
	gchar *tmp_xmame_target, *tmp_xmame_version;
	gdouble version;
	gchar *version_str;
	gboolean xmame_ok = FALSE;

	g_return_val_if_fail ((exec != NULL) || (exec->priv->path != NULL), FALSE);
	g_return_val_if_fail ((g_file_test (exec->priv->path, G_FILE_TEST_IS_EXECUTABLE)), FALSE);

	if (exec->priv->version)
	{
		g_free (exec->priv->version);
		exec->priv->version = NULL;
	}
	if (exec->priv->target)
	{
		g_free (exec->priv->target);
		exec->priv->target = NULL;
	}
	if (exec->priv->name)
	{
		g_free (exec->priv->name);
		exec->priv->name = NULL;
	}

	exec->priv->noloadconfig_option = noloadconfig_option_name[0];
	exec->priv->showusage_option = showusage_option_name[0];
	exec->priv->type = XMAME_EXEC_UNKNOWN;

	opt = g_strdup_printf ("%s -version 2>&1", exec->priv->path);	/* Note use 2>&1 rather than 2>/dev/null so if call fails the error is captured */
	GMAMEUI_DEBUG ("Trying %s", opt);
	xmame_pipe = popen (opt, "r");
	g_free (opt);
	if (!xmame_pipe)
	{
		GMAMEUI_DEBUG ("Could not execute.");
		return FALSE;
	}
	while (fgets (line, BUFFER_SIZE, xmame_pipe) && !xmame_ok)
	{
		GMAMEUI_DEBUG ("Reading line %s", line);

#ifdef ENABLE_XMAME_SVGALIB_SUPPORT
		/* detect svgalib target */
		if (!strncmp (line, "[svgalib:", 9))
		{
			pclose (xmame_pipe);
			return mamesvga_executable_set_version (exec);
		}
#endif

/*#ifdef ENABLE_WINMAME_SUPPORT*/
		/* Probably win32 mame */
		if (!strncmp (line, "Error: unknown option: -version", 31)) {
			pclose (xmame_pipe);
			return winmame_executable_set_version (exec);
		}
/*#endif*/
		/* that should catch any info displayed before the version name */
		if (!strncmp (line, "info", 4) )
			continue;
		/* try to find the '(' */
		for (p = line; (*p && (*p != '(')); p++);
		if (!*p)
			continue;
		* (p-1) = '\0';
		p++;
		tmp_xmame_target = p;

		/* now try to find the ')' */
		for (; (*p && (*p != ')')); p++);
		if (!*p)
			continue;
		*p = '\0';
		p += 2;
		tmp_xmame_version = p;
		
		/* do I need that ? */
		for (; (*p && (*p != '\n'));p++);
		*p = '\0';
		if (strncmp (tmp_xmame_version, "version", 7))
			continue;
			
		GMAMEUI_DEBUG ("checking xmame version: %s (%s) %s", line, tmp_xmame_target, tmp_xmame_version);
			
		exec->priv->name = g_strdup (line);
		exec->priv->target = g_strdup (tmp_xmame_target);
		exec->priv->version = g_strdup (tmp_xmame_version);

		xmame_ok = TRUE;
	}
		
	pclose (xmame_pipe);
		
	if (xmame_ok)
	{
		GMAMEUI_DEBUG ("name=%s. target=%s. version=%s.",
			      exec->priv->name,
			      exec->priv->target,
			      exec->priv->version);

		if (!strcmp (exec->priv->target, "x11"))
			exec->priv->type = XMAME_EXEC_X11;
#ifdef ENABLE_XMAME_SVGALIB_SUPPORT
		else if (!strcmp (exec->priv->target, "svgalib"))
			exec->priv->type = XMAME_EXEC_SVGALIB;
#endif
		else if (!strcmp (exec->priv->target, "ggi"))
			exec->priv->type = XMAME_EXEC_GGI;
		else if (!strcmp (exec->priv->target, "xgl"))
			exec->priv->type = XMAME_EXEC_XGL;
		else if (!strcmp (exec->priv->target, "xfx"))
			exec->priv->type = XMAME_EXEC_XFX;
		else if (!strcmp (exec->priv->target, "svgafx"))
			exec->priv->type = XMAME_EXEC_SVGAFX;
		else if (!strcmp (exec->priv->target, "SDL"))
			exec->priv->type = XMAME_EXEC_SDL;
		else if (!strcmp (exec->priv->target, "photon2"))
			exec->priv->type = XMAME_EXEC_PHOTON2;
		else
			exec->priv->type = XMAME_EXEC_UNKNOWN;
	}

	/* first get the version number to compare it to check SDL modes */
	if (exec->priv->version) {
		version_str = exec->priv->version;

		while (*version_str && !isdigit (*version_str))
			version_str++;

		version = g_ascii_strtod (version_str, NULL);

		if (version == 0.68)
			exec->priv->showusage_option = showusage_option_name[1];
	} else {
		exec->priv->version = g_strdup ("Unknown");
	}
	return xmame_ok;	
}

/* This function compares the raw version (i.e. the 0.xxx number)
   excluding any leading letters or trailing data. For example, a
   version number 'v0.121u2 (Dec  6 2007)' would be transformed
   to 0.121u2. This is compared against the string-format version
   number supplied */
int
mame_compare_version (const MameExec *exec, const gchar *version)
{
	return strcmp (exec->priv->version, version);
}

int
mame_compare_raw_version (const MameExec *exec,
		       const gchar           *version)
{
	g_return_val_if_fail (exec != NULL, FALSE);
	
	gchar *rawversion = g_strdup (exec->priv->version);

	rawversion = strstr (rawversion, "0");
	gchar *p = strstr (rawversion, " ");
	*p = '\0';

/*	if (!strcmp (rawversion, "0.110"))
		GMAMEUI_DEBUG ("Version is same as 0.110");
	else if (strcmp (rawversion, "0.110") > 0)
		GMAMEUI_DEBUG ("Version is greater than 0.110");
	else GMAMEUI_DEBUG ("Version is lower than 0.110");*/
	return strcmp (rawversion, version);
}

/*
 SDLMAME options are generally:
 -<option>      <description>
# (i.e. a header or comment)
 
 XMAME options are more complicated, and can be either:
 -<option>/-<alternative>       <description> (where description is multiple aligned lines)
 -[no]<option>  <description>  
 -<option> <string>     <description>
 First char will be -, space or *
 
 *** (i.e. a header or comment)
 
 */
static MameOption *
parse_option (gchar *line,
	      FILE  *xmame_pipe,
	      int   *more_input)
{
	MameOption *opt;
	gchar *p;
	gchar *start_p;
	gboolean noopt;
	gboolean list_mode;
	gboolean can_have_list;
	const gchar *gmameui_name;
	gboolean option_is_alternative = FALSE;
	GSList *values = NULL;
	
	/* This line is a comment or part of the previous line */
	if (line[0] != '-')
	{
		*more_input = (fgets (line, BUFFER_SIZE, xmame_pipe) != NULL);
		return NULL;
	}
	start_p = line + 1;

	/* check for [no] */
	noopt = !strncmp ("[no]", start_p, 4);

	/* skip [no] */
	if (noopt)
		start_p += 4;
	
	for (p = start_p; *p && *p != ' '; p++);

	*p = '\0';
	
	/* Lookup in mapping hash table and check if we know the option */
	gmameui_name = xmame_option_get_gmameui_name (start_p);

	if (!gmameui_name) {
		*more_input = (fgets (line, BUFFER_SIZE, xmame_pipe) != NULL);
		return NULL;
	}

	opt = (MameOption*) malloc (sizeof (MameOption));
	opt->no_option = noopt;
	opt->name = g_strdup (start_p);
	opt->type = NULL;
	opt->description = NULL;
	opt->possible_values = NULL;
	opt->keys = NULL;

	/* Skip spaces */
	start_p = p + 1;
	while (*start_p == ' ')
		start_p++;

	/* alternative option name */
	if (*start_p == '/') {
		start_p++;

		/* Skip spaces */
		while (*start_p == ' ')
			start_p++;

		/* alternative option is on the next line */
		if (*start_p != '-') {
			option_is_alternative = TRUE;
		}

		/* Skip alternative option name */
		while (*start_p != ' ' && *start_p != '\0')
			start_p++;


		/* Skip spaces */
		while (*start_p == ' ')
			start_p++;
	}

	/* Option type */
	if (*start_p == '<') {
		start_p++;

		for (p = start_p; *p && *p != '>'; p++);

		if (*p == '\0')
			return opt;

		*p = '\0';

		opt->type = g_strdup (start_p);

		start_p = p + 1;

		/* Skip spaces */
		while (*start_p == ' ')
			start_p++;
	}

	/* Strip newline */
	for (p = start_p; *p && *p != '\n'; p++);
	*p = '\0';

	start_p = g_strchomp (start_p); /* Strip space at the end */

	opt->description = g_strdup (start_p);
	list_mode = FALSE;
	can_have_list = (opt->type) && !strcmp (opt->type, "int");

	while ( (*more_input = (fgets (line, BUFFER_SIZE, xmame_pipe) != NULL))) {
		start_p = line;

		if (option_is_alternative) {
			option_is_alternative = FALSE;
			if (*start_p == '-') {
				while (*start_p != ' ' && *start_p != '\0')
					start_p++;
			}
		}

		if (*start_p == '-' || *start_p == '*' || *start_p == '\0' || *start_p == '\n' || *start_p == '\r')
			break;

		/* Option type */
		if (*start_p == '<') {
			start_p++;

			for (p = start_p; *p && *p != '>'; p++);

			if (*p == '\0')
				return opt;

			*p = '\0';

			opt->type = g_strdup (start_p);

			start_p = p + 1;

			/* Skip spaces */
			while (*start_p == ' ')
				start_p++;
		} 
		start_p = g_strchug (start_p); /* Skip spaces */

		/* Strip newline */
		for (p = start_p; *p && *p != '\n'; p++);
		*p = '\0';

		start_p = g_strchomp (start_p); /* Strip space at the end */

		/* If the line starts with the number then it's a list item */
		if (g_ascii_isdigit (start_p[0]) && can_have_list) {
			gchar *orig_start_p;
			int opening_brackets = 0;
			int closing_brackets = 0;

			orig_start_p = start_p;
			/* skip the number */
			while (g_ascii_isdigit (*start_p))
				start_p++;
	
			/* skip spaces and = */
			while (*start_p == ' ' || *start_p == '=')
				start_p++;		

			/* if we havent started list mode try harder to see if 
			it's a number that is part of the description.
			*/
			if (!list_mode) {

				/* count opening and closing brackets */
				for (p = start_p; *p; p++) {
					if (*p == '(')
						opening_brackets++;
					else if (*p == ')')
						closing_brackets++;
				}

				/* if we have more closing than opening brackets
				this means that we are continuing from a previous line.
				*/
				if (closing_brackets > opening_brackets) {
					gchar *new_desc;

					new_desc = g_strdup_printf ("%s %s", opt->description, orig_start_p);
					g_free (opt->description);
					opt->description = new_desc;
				} else
					list_mode = TRUE;
			}

			if (list_mode)
				values = g_slist_append (values, g_strdup (start_p));

		} else {
			gchar *new_desc;

			/* if we are on list mode then append this to the last item. */
			if (list_mode && values) {
				GSList *last_item = g_slist_last (values);

				new_desc = g_strdup_printf ("%s %s", (gchar*)last_item->data, start_p);
				g_free (last_item->data);
				last_item->data = new_desc;
			} else { /* otherwise append it to the description */
				new_desc = g_strdup_printf ("%s %s", opt->description, start_p);
				g_free (opt->description);
				opt->description = new_desc;
			}
		}
	}

	if (values) {
		guint i;
		guint list_size = g_slist_length (values);
		GSList *tmp_list = values;

		opt->possible_values = (gchar**) malloc (sizeof (gchar*) * (list_size + 1));
		for (i =0; i < list_size; i++) {
			opt->possible_values[i] = tmp_list->data;
			tmp_list = g_slist_next (tmp_list);
		}
		opt->possible_values[list_size] = NULL;

		g_slist_free (values);
	}

	return opt;
}

const gchar *
mame_get_option_name (const MameExec *exec,
		       const gchar           *option_name)
{
	const MameOption *opt;

	GMAMEUI_DEBUG ("Checking whether %s is supported under %s (%s)...",
	               option_name,
	               mame_exec_get_name (exec),
	               mame_exec_get_version (exec));

	/* If the Executable has no options hash table,
	   then implicitly create it */
	if (!exec->priv->supported_options)
		exec->priv->supported_options = mame_get_options (exec);
	
	g_return_val_if_fail (exec->priv->supported_options != NULL, NULL);
	
	opt = g_hash_table_lookup (exec->priv->supported_options, option_name);
	if (!opt) {
		GMAMEUI_DEBUG ("    ... no");
		return NULL;
	}

	GMAMEUI_DEBUG ("    ... yes");
	
	return opt->name;
}

const gchar **
mame_get_option_keys (const MameExec *exec,
		       const gchar           *option_name)
{
	const MameOption *opt = g_hash_table_lookup (exec->priv->supported_options, option_name);
	if (!opt)
		return NULL;

	return (const gchar **)opt->keys;
}

const gchar *
mame_get_option_key_value (const MameExec *exec,
			    const gchar           *option_name,
			    const gchar           *key)
{
	gchar **keys;
	gchar **values;
	int count;
	int key_count;
	
	g_return_val_if_fail (key != NULL, NULL);
	
	const MameOption *opt = g_hash_table_lookup (exec->priv->supported_options, option_name);
	g_return_val_if_fail (opt != NULL, NULL);

	keys = opt->keys;
	values = opt->possible_values;

	if (!values || !keys)
		return NULL;

	for (count = 0; keys[count]; count++) {
		if (!strcmp (keys[count], key))
			break;
	}
	if (keys[count]) {
		for (key_count = 0; values[key_count] && key_count < count; key_count++);
		
		return values[key_count];
	}

	return NULL;
}

const gchar **
mame_get_option_values (const MameExec *exec,
			 const gchar           *option_name)
{
	const MameOption *opt = g_hash_table_lookup (exec->priv->supported_options, option_name);
	g_return_val_if_fail (opt != NULL, NULL);

	return (const gchar **)opt->possible_values;
}

const gchar *
mame_get_option_value (const MameExec *exec,
			const gchar           *option_name,
			int                    index)
{
	gchar **values;
	int count;
	const MameOption *opt = g_hash_table_lookup (exec->priv->supported_options, option_name);
	g_return_val_if_fail (opt != NULL, NULL);

	values = opt->possible_values;
	g_return_val_if_fail (values != NULL, NULL);

	for (count = 0; values[count] && count < index; count++);

	return values[count];
}

int
mame_get_option_value_count (const MameExec *exec,
			      const gchar           *option_name)
{
	gchar **values;
	int count;
	const MameOption *opt = g_hash_table_lookup (exec->priv->supported_options, option_name);
	g_return_val_if_fail (opt != NULL, -1);

	values = opt->possible_values;
	g_return_val_if_fail (values != NULL, -1);

	for (count = 0; values[count]; count++);

	return count;
}

gchar *
mame_get_option_string (const MameExec *exec,
			 const gchar           *option_name,
			 const gchar           *arguments)
{
	MameOption *opt;
	gchar *stripped_args;
	gchar *escaped_args;
	gchar *opt_string;

	g_return_val_if_fail (exec->priv->supported_options != NULL, NULL);

	opt = g_hash_table_lookup (exec->priv->supported_options, option_name);
	g_return_val_if_fail (opt != NULL, NULL);

	/* FIXME Newer versions (i.e. SDLMame no longer use <int> to designate option types.
	   Temporarily commenting this out */
	if (!arguments/* || !opt->type*/) {
		GMAMEUI_DEBUG ("xmame_get_option_string: No arguments supplied, or no opt type for option %s", option_name);
		opt_string  = NULL;
	} else {
		stripped_args = g_strdup (arguments);
		stripped_args = g_strstrip (stripped_args);	/* Strip whitespace */
		if (strlen (stripped_args) == 0) {
			g_free (stripped_args);
			return NULL;
		}
		escaped_args = g_shell_quote (stripped_args);
		opt_string =  g_strdup_printf ("-%s %s", opt->name, escaped_args);
		g_free (stripped_args);
		g_free (escaped_args);
	}

	return opt_string;
}

gchar *
mame_get_float_option_string (const MameExec *exec,
			       const gchar           *option_name,
			       float                  argument,
			       char                  *float_buf)
{
	MameOption *opt;
	gchar *opt_string;
	const gchar *opt_value_string;

	g_return_val_if_fail (exec->priv->supported_options != NULL, NULL);

	opt = g_hash_table_lookup (exec->priv->supported_options, option_name);

	if (!opt || !opt->type)
		return NULL;
	
	if (strcmp (opt->type, "float") && strcmp (opt->type, "arg"))
		return NULL;
	
	opt_value_string = my_dtostr (float_buf, argument);
	opt_string = mame_get_option_string (exec, option_name, opt_value_string);

	return opt_string;
}

gchar *
mame_get_int_option_string (const MameExec *exec,
			     const gchar           *option_name,
			     int                    argument)
{
	MameOption *opt;
	gchar *opt_string;
	gchar *opt_value_string;

	g_return_val_if_fail (exec->priv->supported_options != NULL, NULL);

	opt = g_hash_table_lookup (exec->priv->supported_options, option_name);
	g_return_val_if_fail (opt != NULL, NULL);
	
	if (!opt->type || strcmp (opt->type, "int"))
		return NULL;
	
	opt_value_string = g_strdup_printf (" %i", argument);
	
	opt_string = mame_get_option_string (exec, option_name, opt_value_string);
	g_free (opt_value_string);

	return opt_string;
}

gchar *
mame_get_boolean_option_string (const MameExec *exec,
				const gchar *option_name,
				gboolean is_enabled)
{
	MameOption *opt;
	gchar *opt_string;
	
	g_return_val_if_fail (exec->priv->supported_options != NULL, NULL);

	opt = g_hash_table_lookup (exec->priv->supported_options, option_name);
	g_return_val_if_fail (opt != NULL, NULL);

	/* boolean options have no type */
	if (opt->type)
		return NULL;

	if (opt->no_option)
		opt_string = g_strdup_printf ("-%s%s", (is_enabled) ? "" : "no", opt->name);
	else if (is_enabled)
		opt_string = g_strdup_printf ("-%s", opt->name);
	else
		opt_string = NULL;

	return opt_string;
}

FILE *
mame_open_pipe (const MameExec *exec, const gchar *format, ...)
{
	va_list args;
	gchar *my_args;
	gchar *opt;
	FILE *mame_pipe;
	
	va_start (args, format);
	my_args = g_strdup_vprintf (format, args);
	va_end (args);

	opt = g_strdup_printf ("%s -%s %s 2>/dev/null",
			       exec->priv->path,
			       exec->priv->noloadconfig_option,
			       my_args);
	GMAMEUI_DEBUG ("Running: %s", opt);
	mame_pipe = popen (opt, "r");

	g_free (opt);
	g_free (my_args);

	return mame_pipe;
}

void
mame_close_pipe (const MameExec *exec, FILE *pipe)
{
	pclose (pipe);
}

static gchar **
slist_to_array (GSList *list)
{
	guint i;
	GSList *tmp_list;
	gchar **array;
	guint list_size;
	
	g_return_val_if_fail (list != NULL, NULL);
			
	list_size = g_slist_length (list);
	tmp_list = list;

	array = (gchar**) malloc (sizeof (gchar*) * (list_size + 1));
	for (i =0; i < list_size; i++) {
		array[i] = tmp_list->data;
		tmp_list = g_slist_next (tmp_list);
	}
	array[list_size] = NULL;
	g_slist_free (list);

	return array;
}

static void
parse_list_option (MameExec *exec,
		   const gchar     *option_name,
		   const gchar     *list_option)
{
	MameOption *option;
	gchar *key, *value;
	gchar *keyword, *name, *p;
	gchar line[BUFFER_SIZE];
	GSList *value_key = NULL;
	GSList *value_value = NULL;
	FILE *xmame_pipe;
	int i;

	if (!mame_has_option (exec, list_option) || !mame_has_option (exec, option_name))
		return;

	option = (MameOption*)mame_get_option (exec, option_name);

	xmame_pipe = mame_open_pipe (exec, "-%s", mame_get_option_name (exec, list_option));
	g_return_if_fail (xmame_pipe != NULL);
	
	/* header : Digital sound plugins: */
	fgets (line, BUFFER_SIZE, xmame_pipe);
	/* first empty line */
	fgets (line, BUFFER_SIZE, xmame_pipe);
	
	while (fgets (line, BUFFER_SIZE, xmame_pipe))
	{
		/* prevent to get other things as plugins (Open GL copyright line) */
		if (line[0] == '\n')
			break;
		else
		{
			/* remove traling \n */
			line[strlen (line) - 1] = 0;
			/* find the end of the keyword */
			for (i = 1, keyword = p = line; (*p && (*p++ != ' ')); i++);
			keyword[i] = '\0';
			/* find the begining of the plugin complete name */
			for (i = 0, name = ++p; (*p && (*p++ == ' ')); i++);
			g_strstrip (name);
			GMAMEUI_DEBUG ("plugin found: %s, code (%s)", name, keyword);
			key = g_strndup (keyword, strlen (keyword)-1);
			value = g_strdup (name);
			value_key = g_slist_append (value_key, key);
			value_value = g_slist_append (value_value, value);
		}
	}

	mame_close_pipe (exec, xmame_pipe);

	if (value_key) {
		if (option->keys)
			g_strfreev (option->keys);
		if (option->possible_values)
			g_strfreev (option->possible_values);

		option->keys = slist_to_array (value_key);
		option->possible_values = slist_to_array (value_value);
	}
}

static void
parse_listmodes_option (MameExec *exec,
			const gchar     *option_name,
			const gchar     *list_option)
{
	MameOption *option;
	gchar *p, *name, *key, *value, *keyword;
	GSList *value_key = NULL;
	GSList *value_value = NULL;
	gchar line[BUFFER_SIZE];
	FILE *xmame_pipe;
	int i;
	
	option = (MameOption*) mame_get_option (exec, option_name);
	
	if (!option || !mame_has_option (exec, list_option))
		return;
		
	xmame_pipe = mame_open_pipe (exec, "-%s", mame_get_option_name (exec, list_option));
	g_return_if_fail (xmame_pipe != NULL);

	GMAMEUI_DEBUG ("getting xmame SDL mode");
	while (fgets (line, BUFFER_SIZE, xmame_pipe))
	{
		if (!strncmp (line, "Modes available:", 16))
		{
				GMAMEUI_DEBUG ("begin mode listing");
				while (fgets (line, BUFFER_SIZE, xmame_pipe))
				{
					if (line[0] == '\n')
						break;
					else
					{
						/* remove traling \n */
						line[strlen (line) - 1] = 0;
						/* find the end of the keyword */
						for (i = 1, keyword = p = line; (*p && (*p++ != ')')); i++);
						keyword[i] = '\0';
						g_strstrip (keyword);
						/* find the begining of the plugin complete name */
						name = p+6;
						g_strstrip (name);
						GMAMEUI_DEBUG ("Resolution found: %s, code (%s)", name, keyword);
						key = g_strndup (keyword, strlen (keyword) - 1);
						value = g_strdup (name);
						
						value_key = g_slist_append (value_key, key);
						value_value = g_slist_append (value_value, value);
					}
				}
		}
	}

	mame_close_pipe (exec, xmame_pipe);
	
	if (value_key) {
		if (option->keys)
			g_strfreev (option->keys);
		if (option->possible_values)
			g_strfreev (option->possible_values);

		option->keys = slist_to_array (value_key);
		option->possible_values = slist_to_array (value_value);
	}
}

/* Sets up an IO channel that calls func whenever data is available */
GIOChannel *
mame_executable_set_up_io_channel (gint fd, GIOCondition cond, GIOFunc func, gpointer data)
{
	GIOChannel *ioc;

	/* set up handler for data */
	ioc = g_io_channel_unix_new (fd);

	g_io_channel_set_encoding (ioc, NULL, NULL);
	/*g_io_channel_set_buffered (ioc, FALSE);*/

	/* Tell the io channel to close the file descriptor
	 *  when the io channel gets destroyed */
	g_io_channel_set_close_on_unref (ioc, TRUE);

	g_io_channel_set_flags (ioc, G_IO_FLAG_NONBLOCK, NULL);
	
	/* g_io_add_watch() adds its own reference,
	 *  which will be dropped when the watch source
	 *  is removed from the main loop (which happens
	 *  when we return FALSE from the callback) */
	g_io_add_watch (ioc, cond, func, data);
	g_io_channel_unref (ioc);

	GMAMEUI_DEBUG ("Set up IO channel on fd %d", fd);
	
	return ioc;
}

/*
 * Execute a MAME command
 * command is a string
 * command_pid is the address of a pid_t to store the process ID of the launched process
 * child_stdout is the descripter of the stdout stream
 * child_stderr is the descripter of the stderr stream
 * e.g. mame_exec_launch_command (command, &command_pid, &child_stdout, &child_stderr); 
 */
void
mame_exec_launch_command (gchar *command, pid_t *pid, int *stdout, int *stderr) {
	gchar **argv  = NULL;
	GError *error = NULL;
	
	if (!g_shell_parse_argv (command, NULL, &argv, &error)) {
		GMAMEUI_DEBUG ("Could not parse MAME command line arguments: %s", error->message);
		g_error_free (error);
		error = NULL;
		return;
	}

	int arg = 0;
	GMAMEUI_DEBUG ("Command line for command is:");
	while (argv[arg] != NULL) {
		printf("%s ", argv[arg++]);
	}
	printf("\n");

	/* Note - passing pointers to the fds, so will not prefix the pointer &,
	   in contrast to most tutorials or examples */
	if (!g_spawn_async_with_pipes (NULL, argv, NULL,
				       G_SPAWN_DO_NOT_REAP_CHILD,
				       NULL, NULL,
				       pid,
				       NULL,
				       stdout,
				       stderr,
				       &error)) {
		GMAMEUI_DEBUG ("Error spawning MAME command: %s", error->message);
		g_error_free (error);
		error = NULL;
	} else {
		GMAMEUI_DEBUG ("Created new child process with pid %lu", (gulong) *pid);
	}

	g_strfreev (argv);
	argv = NULL;
}













static void
mame_opt_free (gpointer opt)
{
	MameOption *opt_opt =  opt;
	xmame_option_free (opt_opt);
}

const GHashTable *
mame_get_options (MameExec *exec)
{
	FILE *xmame_pipe;
	gchar line[BUFFER_SIZE];
	GHashTable *option_hash;

	g_return_val_if_fail (exec != NULL, NULL);

	/* If the hash table of supported options exists, return it */
	if (exec->priv->supported_options)
		return exec->priv->supported_options;

	/* ... otherwise create it */
	GMAMEUI_DEBUG (_("Getting list of valid MAME options using parameter %s"), exec->priv->showusage_option);
	
	xmame_pipe = mame_open_pipe (exec, "-%s", exec->priv->showusage_option);
	g_return_val_if_fail (xmame_pipe != NULL, NULL);

	if (fgets (line, BUFFER_SIZE, xmame_pipe))
	{
		int more_input = 0;
		option_hash = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, mame_opt_free);
		
		do {
			MameOption *my_opt;
			const gchar *gmameui_name;
			my_opt = parse_option (line, xmame_pipe, &more_input);

			if (my_opt) {
				gmameui_name = xmame_option_get_gmameui_name (my_opt->name);

				/* Note: Newer versions of MAME do not report the type */
				GMAMEUI_DEBUG (_("Found option: %s %s%s (%s)"),
					my_opt->type ? my_opt->type : "",
					my_opt->no_option ? "[no]" : "",
					my_opt->name,
					gmameui_name);
				GMAMEUI_DEBUG (_("Description: \"%s\""), my_opt->description);

				if (my_opt->possible_values) {
					int i;
					GMAMEUI_DEBUG (_("Possible values: "));
					for (i = 0; my_opt->possible_values[i]; i++) {
						GMAMEUI_DEBUG ("%i %s", i, my_opt->possible_values[i]);
					}
				}

				if (gmameui_name) {
					GMAMEUI_DEBUG ("Adding supported option %s", my_opt->name);
					g_hash_table_insert (option_hash, (gpointer)gmameui_name, my_opt);
				}
			}
		} while (more_input);

		/* Delete existing options hash table */
		if (exec->priv->supported_options)
			g_hash_table_destroy (exec->priv->supported_options);

		exec->priv->supported_options = option_hash;
	}

	mame_close_pipe (exec, xmame_pipe);

	parse_list_option (exec, "dsp-plugin", "list-dsp-plugins");
	parse_list_option (exec, "sound-mixer-plugin", "list-mixer-plugins");
	parse_listmodes_option (exec, "modenumber", "listmodes");
	
	return exec->priv->supported_options;
}

/* Check whether the option is a toggle option that is turned off by
   using the -no<option> syntax, e.g. -nolog. Some versions of XMAME
   (e.g. 0.92) simply omit the option, others (e.g. 0.106) use -nolog */
gboolean
mame_option_supports_no_prefix (MameExec *exec, const gchar *option_name)
{
	g_return_val_if_fail (exec != NULL, FALSE);
	
	const MameOption *option;
	
	option = g_hash_table_lookup (exec->priv->supported_options, option_name);
	
	if (!option) {
		GMAMEUI_DEBUG ("The option %s does not have a listing in the hash table", option_name);
		return FALSE;
	}
	
	return option->no_option;
}

gboolean
mame_has_option (MameExec *exec, const gchar *option_name)
{
	g_return_val_if_fail (exec != NULL, FALSE);
	g_return_val_if_fail (option_name != NULL, FALSE);
	
	return mame_get_option (exec, option_name) != NULL;
}

const MameOption *
mame_get_option (MameExec *exec, const gchar *option_name)
{
	g_return_val_if_fail (exec != NULL, NULL);

	/* If the Executable has no options hash table,
	   then implicitly create it */
	if (!exec->priv->supported_options)
		exec->priv->supported_options = mame_get_options (exec);

#ifdef ENABLE_DEBUG
	if (!mame_option_get_gmameui_name (option_name))
		GMAMEUI_DEBUG ("Invalid option: %s", option_name);
#endif
	return g_hash_table_lookup (exec->priv->supported_options, option_name);

}



/*

static gboolean
winmame_exec_set_version (MameExec *exec)
{
TODO
}*/


/* Gets the name for the MameExec. Name should not be free'd */
gchar*
mame_exec_get_name (MameExec *exec)
{
	g_return_val_if_fail (exec != NULL, NULL);
	g_return_val_if_fail (exec->priv != NULL, NULL);
	g_return_val_if_fail (exec->priv->name != NULL, NULL);

	return exec->priv->name;
}

/* Gets the path for the MameExec. Path should not be free'd */
gchar*
mame_exec_get_path (MameExec *exec)
{
	g_return_val_if_fail (exec != NULL, NULL);
	g_return_val_if_fail (exec->priv != NULL, NULL);
	g_return_val_if_fail (exec->priv->path != NULL, NULL);

	return exec->priv->path;
}

/* Gets the version for the MameExec. Version should not be free'd */
gchar*
mame_exec_get_version (MameExec *exec)
{
	g_return_val_if_fail (exec != NULL, NULL);
	g_return_val_if_fail (exec->priv != NULL, NULL);
	g_return_val_if_fail (exec->priv->version != NULL, NULL);

	return exec->priv->version;
}

gchar*
mame_exec_get_noloadconfig_option (MameExec *exec)
{
	g_return_val_if_fail (exec != NULL, NULL);
	g_return_val_if_fail (exec->priv != NULL, NULL);
	g_return_val_if_fail (exec->priv->noloadconfig_option != NULL, NULL);

	return exec->priv->noloadconfig_option;
}

/* Gets the target for the MameExec. Target should not be free'd */
gchar*
mame_exec_get_target (MameExec *exec)
{
	g_return_val_if_fail (exec != NULL, NULL);
	g_return_val_if_fail (exec->priv != NULL, NULL);
	g_return_val_if_fail (exec->priv->target != NULL, NULL);

	return exec->priv->target;
}

ExecutableType
mame_exec_get_exectype (MameExec *exec)
{
	g_return_val_if_fail (exec != NULL, XMAME_EXEC_UNKNOWN);

	return exec->priv->type;
}

void
mame_exec_set_exectype (MameExec *exec, ExecutableType type)
{
	g_return_if_fail (exec != NULL);
	
	exec->priv->type = type;
}

static void
mame_exec_finalize (GObject *obj)
{
	GMAMEUI_DEBUG ("Finalising mame_exec object");
	
	MameExec *exec = MAME_EXEC (obj);
	
	g_free (exec->priv->path);
	g_free (exec->priv->name);
	g_free (exec->priv->target);
	g_free (exec->priv->version);
	
// FIXME TODO	g_free (exec->priv);
	
	GMAMEUI_DEBUG ("Finalising mame_exec object... done");
	
	/* FIXME TODO Unref all the strings and destroy the object */
}

static void
mame_exec_class_init (MameExecClass *klass)
{
	
	
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	object_class->set_property = mame_exec_set_property;
	object_class->get_property = mame_exec_get_property;
	object_class->finalize = mame_exec_finalize;

    g_object_class_install_property (object_class,
					 PROP_EXEC_PATH,
					 g_param_spec_string ("exec-path", "Executable path", "", NULL, G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
					 PROP_EXEC_NAME,
					 g_param_spec_string ("exec-name", "Executable name", "", NULL, G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
					 PROP_EXEC_TARGET,
					 g_param_spec_string ("exec-target", "Executable target", "", NULL, G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
					 PROP_EXEC_VERSION,
					 g_param_spec_string ("exec-version", "Executable version", "", NULL, G_PARAM_READWRITE));
}

static void
mame_exec_init (MameExec *exec)
{
	
	GMAMEUI_DEBUG ("Creating mame_exec object");
	exec->priv = g_new0 (MameExecPrivate, 1);

	exec->priv->name = NULL;
	exec->priv->version = NULL;
	exec->priv->target = NULL;
	exec->priv->path = NULL;

	exec->priv->noloadconfig_option = noloadconfig_option_name[0];
	exec->priv->showusage_option = showusage_option_name[0];
	exec->priv->type = XMAME_EXEC_UNKNOWN;

}

MameExec* mame_exec_new (void)
{
	return g_object_new (MAME_TYPE_EXEC, NULL);
}

static void
mame_exec_set_property (GObject *object,
			     guint prop_id,
			     const GValue *value,
			     GParamSpec *pspec)
{
	MameExec *exec;
	
	exec = MAME_EXEC (object);

	switch (prop_id) {
		case PROP_EXEC_PATH:
			exec->priv->path = g_strdup (g_value_get_string (value));
			break;
		case PROP_EXEC_NAME:
			exec->priv->name = g_strdup (g_value_get_string (value));
			break;
		case PROP_EXEC_TARGET:
			exec->priv->target = g_strdup (g_value_get_string (value));
			break;
		case PROP_EXEC_VERSION:
			exec->priv->version = g_strdup (g_value_get_string (value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
mame_exec_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	MameExec *exec;

	exec = MAME_EXEC (object);

	switch (prop_id) {
		case PROP_EXEC_PATH:
			g_value_set_string (value, exec->priv->path);
			break;
		case PROP_EXEC_NAME:
			g_value_set_string (value, exec->priv->name);
			break;
		case PROP_EXEC_TARGET:
			g_value_set_string (value, exec->priv->target);
			break;
		case PROP_EXEC_VERSION:
			g_value_set_string (value, exec->priv->version);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

MameExec* mame_exec_new_from_path (gchar *path)
{
	MameExec *exec;

	g_return_val_if_fail (path != NULL, NULL);

	GMAMEUI_DEBUG ("Creating new executable from path %s", path);

	/* Check that the path exists and is a valid executable; if not
	   return NULL */
	if (!g_file_test (path, G_FILE_TEST_EXISTS))
		return NULL;

	if (!g_file_test (path, G_FILE_TEST_IS_EXECUTABLE))
		return NULL;

	exec = g_object_new (MAME_TYPE_EXEC, NULL);

	exec->priv->path = g_strdup (path);

	if (mame_exec_set_version (exec) == FALSE)
		return NULL;

	return exec;
}

int mame_exec_get_game_count(MameExec *exec)
{
	FILE *handle;
	char buffer[1024];
	gchar *option;
	int ret = 0;
	int count = 0;

	if (!exec)
		return 0;

	mame_get_options (exec);

	option = g_strdup (mame_get_option_name (exec, "listfull"));
	GMAMEUI_DEBUG ("Retrieving full game list using %s.", option);

	handle = mame_open_pipe(exec, "-%s", option);

	GMAMEUI_DEBUG ("Game list retrieved... parsing.");
	
	while(fgets(buffer, 1024, handle))
	{
		/* Skip the header row */
		if (!strncmp(buffer, "Name:     Description:", 22))
			continue;

		static char keywork[] = "Total Supported: ";
		if(!strncmp(buffer, keywork, sizeof(keywork) -1))
		{
			ret = atoi(buffer + sizeof(keywork) -1);
			break;
		}
		
		count++;
	}
	pclose(handle);

	/* If there was no line stating Total Supported:, then we have
	   to get a list of all rows returned (subtracting the header)
	   to get the final count */
	if (ret == 0) ret = count;
	
	GMAMEUI_DEBUG ("Game count obtained - %d", ret);
	
	g_free (option);

	return ret;
}