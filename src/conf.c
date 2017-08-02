/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <acfutils/assert.h>
#include <acfutils/avl.h>
#include <acfutils/conf.h>
#include <acfutils/helpers.h>
#include <acfutils/log.h>

/*
 * This is a general-purpose configuration store. It's really just a
 * key-value pair dictionary that can be read from and written to a
 * file.
 */

struct conf {
	avl_tree_t	tree;
};

typedef struct {
	char		*key;
	char		*value;
	avl_node_t	node;
} conf_key_t;

static void
strtolower(char *str)
{
	for (; *str != 0; str++)
		*str = tolower(*str);
}

static int
conf_key_compar(const void *a, const void *b)
{
	const conf_key_t *ka = a, *kb = b;
	int c = strcmp(ka->key, kb->key);
	if (c < 0)
		return (-1);
	else if (c == 0)
		return (0);
	else
		return (1);
}

/*
 * Creates an empty configuration. Set values using conf_set_* and write
 * to a file using conf_write,
 */
conf_t *
conf_create_empty(void)
{
	conf_t *conf = calloc(1, sizeof (*conf));
	avl_create(&conf->tree, conf_key_compar, sizeof (conf_key_t),
	    offsetof(conf_key_t, node));
	return (conf);
}

/*
 * Frees a conf_t object and all of its internal resources.
 */
void
conf_free(conf_t *conf)
{
	void *cookie = NULL;
	conf_key_t *ck;

	while ((ck = avl_destroy_nodes(&conf->tree, &cookie)) != NULL) {
		free(ck->key);
		free(ck->value);
		free(ck);
	}
	avl_destroy(&conf->tree);
	free(conf);
}

/*
 * Parses a configuration from a file. The file is structured as a
 * series of "key = value" lines. The parser understands "#" and "--"
 * comments.
 * Returns the parsed conf_t object, or NULL in case an error was found.
 * If errline is not NULL, it is set to the line number where the error
 * was encountered.
 */
conf_t *
conf_read(FILE *fp, int *errline)
{
	conf_t *conf;
	char *line = NULL;
	size_t linecap = 0;
	int linenum = 0;

	conf = conf_create_empty();

	while (!feof(fp)) {
		char *sep;
		conf_key_t srch;
		conf_key_t *ck;
		avl_index_t where;

		linenum++;
		if (getline(&line, &linecap, fp) <= 0)
			continue;
		strip_space(line);
		if (*line == 0)
			continue;

		if ((sep = strstr(line, "--")) != NULL ||
		    (sep = strstr(line, "#")) != NULL) {
			*sep = 0;
			strip_space(line);
			if (*line == 0)
				continue;
		}

		/* make the config file case-insensitive */
		strtolower(line);

		sep = strstr(line, "=");
		if (sep == NULL) {
			conf_free(conf);
			if (errline != NULL)
				*errline = linenum;
			return (NULL);
		}
		*sep = 0;

		strip_space(line);
		strip_space(&sep[1]);

		srch.key = malloc(strlen(line) + 1);
		strcpy(srch.key, line);
		ck = avl_find(&conf->tree, &srch, &where);
		if (ck == NULL) {
			/* if the key didn't exist yet, create a new one */
			ck = calloc(1, sizeof (*ck));
			ck->key = srch.key;
			avl_insert(&conf->tree, ck, where);
		} else {
			/* key already exists, free the search one */
			free(srch.key);
		}
		free(ck->value);
		ck->value = malloc(strlen(&sep[1]) + 1);
		strcpy(ck->value, &sep[1]);
	}

	free(line);

	return (conf);
}

/*
 * Writes a conf_t object to a file. Returns B_TRUE if the write was
 * successful, B_FALSE otherwise.
 */
bool_t conf_write(const conf_t *conf, FILE *fp)
{
	for (conf_key_t *ck = avl_first(&conf->tree); ck != NULL;
	    ck = AVL_NEXT(&conf->tree, ck)) {
		if (fprintf(fp, "%s = %s\n", ck->key, ck->value) <= 0)
			return (B_FALSE);
	}
	return (B_TRUE);
}

/*
 * Looks for a pre-existing configuration key-value pair based on key name.
 * Returns the conf_key_t object if found, NULL otherwise.
 */
static conf_key_t *
conf_find(const conf_t *conf, const char *key)
{
	const conf_key_t srch = { .key = (char *)key };
	return (avl_find(&conf->tree, &srch, NULL));
}

/*
 * Retrieves the string value of a configuration key. If found, the value
 * is placed in *value. Returns B_TRUE if the key was found, else B_FALSE.
 */
bool_t
conf_get_str(const conf_t *conf, const char *key, const char **value)
{
	const conf_key_t *ck = conf_find(conf, key);
	if (ck == NULL)
		return (B_FALSE);
	*value = ck->value;
	return (B_TRUE);
}

/*
 * Retrieves the 32-bit int value of a configuration key. If found, the value
 * is placed in *value. Returns B_TRUE if the key was found, else B_FALSE.
 */
bool_t
conf_get_i(const conf_t *conf, const char *key, int *value)
{
	const conf_key_t *ck = conf_find(conf, key);
	if (ck == NULL)
		return (B_FALSE);
	*value = atoi(ck->value);
	return (B_TRUE);
}

/*
 * Retrieves the 64-bit float value of a configuration key. If found, the value
 * is placed in *value. Returns B_TRUE if the key was found, else B_FALSE.
 */
bool_t
conf_get_d(const conf_t *conf, const char *key, double *value)
{
	const conf_key_t *ck = conf_find(conf, key);
	if (ck == NULL)
		return (B_FALSE);
	*value = atof(ck->value);
	return (B_TRUE);
}

/*
 * Retrieves the boolean value of a configuration key. If found, the value
 * is placed in *value. Returns B_TRUE if the key was found, else B_FALSE.
 */
bool_t
conf_get_b(const conf_t *conf, const char *key, bool_t *value)
{
	const conf_key_t *ck = conf_find(conf, key);
	if (ck == NULL)
		return (B_FALSE);
	*value = (strcmp(ck->value, "true") == 0 ||
	    strcmp(ck->value, "1") == 0 ||
	    strcmp(ck->value, "yes") == 0);
	return (B_TRUE);
}

/*
 * Sets up a key-value pair in the conf_t structure with a string value.
 * If value = NULL, this instead removes the key-value pair (if present).
 */
void
conf_set_str(conf_t *conf, const char *key, const char *value)
{
	conf_key_t *ck = conf_find(conf, key);

	if (ck == NULL) {
		if (value == NULL)
			return;
		ck = calloc(1, sizeof (*ck));
		ck->key = strdup(key);
		avl_add(&conf->tree, ck);
	}
	free(ck->value);
	if (value == NULL) {
		avl_remove(&conf->tree, ck);
		free(ck->key);
		free(ck);
		return;
	}
	ck->value = strdup(value);
}

static void conf_set_common(conf_t *conf, const char *key,
    const char *fmt, ...) PRINTF_ATTR(3);

/*
 * Common setter back-end for conf_set_{i,d,b}.
 */
static void
conf_set_common(conf_t *conf, const char *key, const char *fmt, ...)
{
	int n;
	conf_key_t *ck = conf_find(conf, key);
	va_list ap1, ap2;

	va_start(ap1, fmt);
	va_copy(ap2, ap1);

	if (ck == NULL) {
		ck = calloc(1, sizeof (*ck));
		ck->key = strdup(key);
		avl_add(&conf->tree, ck);
	}
	free(ck->value);
	n = vsnprintf(NULL, 0, fmt, ap1);
	ASSERT3S(n, >, 0);
	ck->value = malloc(n + 1);
	(void) vsnprintf(ck->value, n, fmt, ap2);
	va_end(ap1);
	va_end(ap2);
}

/*
 * Same as conf_set_str but with an int value. Obviously this cannot
 * remove a key, use conf_set_str(conf, key, NULL) for that.
 */
void
conf_set_i(conf_t *conf, const char *key, int value)
{
	conf_set_common(conf, key, "%i", value);
}

/*
 * Same as conf_set_str but with a double value. Obviously this cannot
 * remove a key, use conf_set_str(conf, key, NULL) for that.
 */
void
conf_set_d(conf_t *conf, const char *key, double value)
{
	conf_set_common(conf, key, "%f", value);
}

/*
 * Same as conf_set_str but with a bool_t value. Obviously this cannot
 * remove a key, use conf_set_str(conf, key, NULL) for that.
 */
void
conf_set_b(conf_t *conf, const char *key, bool_t value)
{
	conf_set_common(conf, key, "%s", value ? "true" : "false");
}
