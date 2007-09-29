#include <svn_repos.h>
#include <svn_pools.h>
#include <svn_error.h>
#include <svn_client.h>
#include <svn_dso.h>
#include <svn_path.h>
#include <svn_config.h>
#include <svn_cmdline.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#define IF_ERROR_RETURN(err, pool, L) do { \
	if (err) { \
		svn_pool_destroy (pool); \
		return send_error (L, err); \
	} \
} while (0)


/* Calls lua_error */
static int
send_error (lua_State *L, svn_error_t *err) {
	lua_pushstring (L, err->message);

	svn_error_clear (err);
	
	return lua_error (L);
}

/* Initializes the memory pool */
static int
init_pool (apr_pool_t **pool) {
	
	if (apr_initialize () != APR_SUCCESS) {
		return 1;
	}

	svn_dso_initialize ();

    *pool = svn_pool_create (NULL);

	return 0;
}


/* Indicates that an error occurred during the pool initialization */
static int
init_pool_error (lua_State *L) {
	svn_error_t *err = malloc (sizeof (svn_error_t *));
	err->message = "Error initializing the memory pool\n";
	return send_error (L, err);
}


static int
init_function (svn_client_ctx_t **ctx, apr_pool_t **pool, lua_State *L) {
	apr_allocator_t *allocator;
	svn_auth_baton_t *ab;
	svn_config_t *cfg;
	svn_error_t *err;

	if (svn_cmdline_init("svn", stderr) != EXIT_SUCCESS) {
		err = malloc (sizeof (svn_error_t *));
		err->message = "Error initializing svn\n";
		return send_error (L, err);
	}

	if (apr_allocator_create(&allocator)) {
		err = malloc (sizeof (svn_error_t *));
		err->message = "Error creating allocator\n";
		return send_error (L, err);
	}

	apr_allocator_max_free_set(allocator, SVN_ALLOCATOR_RECOMMENDED_MAX_FREE);

  	*pool = svn_pool_create_ex(NULL, allocator);
	apr_allocator_owner_set(allocator, *pool);

  	err = svn_ra_initialize(*pool);
	IF_ERROR_RETURN (err, *pool, L);

	err = svn_client_create_context (ctx, *pool);
	IF_ERROR_RETURN (err, *pool, L);

	err = svn_config_get_config(&((*ctx)->config),	NULL, *pool);
	IF_ERROR_RETURN (err, *pool, L);

	cfg = apr_hash_get((*ctx)->config, SVN_CONFIG_CATEGORY_CONFIG,
			APR_HASH_KEY_STRING);


	err = svn_cmdline_setup_auth_baton(&ab,
			FALSE,
			NULL,
			NULL,
			NULL,
			FALSE,
			cfg,
			(*ctx)->cancel_func,
			(*ctx)->cancel_baton,
			*pool);
	IF_ERROR_RETURN (err, *pool, L);

	(*ctx)->auth_baton = ab;

	return 0;
}

static enum svn_opt_revision_kind
get_revision_kind (const char *path) {
	if (svn_path_is_url (path))
		return svn_opt_revision_head;
	return svn_opt_revision_base;
}

static int
l_add (lua_State *L) {
	const char *path = luaL_checkstring (L, 1);

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

	err = svn_client_add3 (path, TRUE, FALSE, FALSE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	svn_pool_destroy (pool);
	
	return 0;
}




svn_error_t *
write_fn (void *baton, const char *data, apr_size_t *len) {

	svn_stringbuf_appendbytes (baton, data, *len);

	return NULL;
}



/* Gets the content of a file 
 * Returns the content of a file */
static int
l_cat (lua_State *L) {
	const char *path = luaL_checkstring (L, 1);

	svn_opt_revision_t peg_revision;
	svn_opt_revision_t revision;

	revision.value.number =  lua_gettop (L) == 2 ? lua_tointeger (L, 2) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = get_revision_kind (path);
	}

	peg_revision = revision;

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

	svn_stream_t *stream;

	stream = svn_stream_empty (pool);
	svn_stream_set_write (stream, write_fn);

	svn_stringbuf_t *buffer = svn_stringbuf_create ("\0", pool);

	svn_stream_set_baton (stream, buffer);

	err = svn_client_cat2 (stream, path, &peg_revision, &revision, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushstring (L, buffer->data);

	svn_pool_destroy (pool);

	return 1;
}


static int
l_checkout (lua_State *L) {
	const char *path = luaL_checkstring (L, 1);
	const char *dir = luaL_checkstring (L, 2);

	svn_opt_revision_t revision;
	svn_opt_revision_t peg_revision;

	peg_revision.kind = svn_opt_revision_unspecified;

	revision.value.number =  lua_gettop (L) == 3 ? lua_tointeger (L, 3) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = get_revision_kind (path);
	}

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);
	dir = svn_path_canonicalize(dir, pool);

	svn_revnum_t rev;

	err = svn_client_checkout2 (&rev, path, dir, &peg_revision, &revision, TRUE, FALSE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);
	
	lua_pushnumber (L, rev);

	svn_pool_destroy (pool);

	return 1;
}

static int
l_cleanup (lua_State *L) {
	const char *path = luaL_checkstring (L, 1);

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

	err = svn_client_cleanup (path, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);
	
	svn_pool_destroy (pool);

	return 0;
}




static int
l_commit (lua_State *L) {
	const char *path = luaL_checkstring (L, 1);

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

	apr_array_header_t *array;

	array = apr_array_make (pool, 1, sizeof (const char *));
	(*((const char **) apr_array_push (array))) = path;

	svn_commit_info_t *commit_info = NULL;

	err = svn_client_commit3 (&commit_info, array, TRUE, FALSE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);	

	if (commit_info == NULL) {
		lua_pushnil (L);
	} else {
		lua_pushnumber (L, commit_info->revision);
	}

	svn_pool_destroy (pool);

	return 1;
}

static int
l_copy (lua_State *L) {
	const char *src_path = luaL_checkstring (L, 1);
	const char *dest_path = luaL_checkstring (L, 2);

	svn_opt_revision_t revision;

	revision.value.number =  lua_gettop (L) == 3 ? lua_tointeger (L, 3) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = get_revision_kind (src_path);
	}

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	src_path = svn_path_canonicalize(src_path, pool);
	dest_path = svn_path_canonicalize(dest_path, pool);

	svn_commit_info_t *commit_info = NULL;

	err = svn_client_copy3 (&commit_info, src_path, &revision, dest_path, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);	

	if (commit_info == NULL) {
		lua_pushnil (L);
	} else {
		lua_pushnumber (L, commit_info->revision);
	}

	svn_pool_destroy (pool);

	return 1;
}


static int
l_delete (lua_State *L) {

	const char *path = luaL_checkstring (L, 1);
	
	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

	apr_array_header_t *array;

	array = apr_array_make (pool, 1, sizeof (const char *));
	(*((const char **) apr_array_push (array))) = path;

	svn_commit_info_t *commit_info;

	lua_newtable (L);

	err = svn_client_delete2 (&commit_info, array, FALSE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushinteger (L, commit_info->revision);
	
	svn_pool_destroy (pool);

	return 1;

}


static int
l_diff (lua_State *L) {
	const char *path1 = luaL_checkstring (L, 1);

	svn_opt_revision_t rev1;

	rev1.value.number = lua_tointeger (L, 2);
	if (rev1.value.number) {
		rev1.kind = svn_opt_revision_number;
	} else {
		rev1.kind = get_revision_kind (path1);
	}
	
	const char *path2 = (lua_gettop (L) < 3 || lua_isnil (L, 3)) ? path1 : luaL_checkstring (L, 3);

	svn_opt_revision_t rev2;

	rev2.value.number = lua_tointeger (L, 4);
	if (rev2.value.number) {
		rev2.kind = svn_opt_revision_number;
	} else {
		if (svn_path_is_url (path2)) {
			rev2.kind = svn_opt_revision_head;
		} else {
			rev2.kind = svn_opt_revision_working;
		}
	}

	const char *outfile = lua_gettop (L) >= 5 ? luaL_checkstring (L, 5) : NULL;
	
	const char *errfile = lua_gettop (L) == 6 ? luaL_checkstring (L, 6) : NULL;
	

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path1 = svn_path_canonicalize(path1, pool);
	path2 = svn_path_canonicalize(path2, pool);

	apr_file_t *aprout;
	apr_file_t *aprerr;
	apr_status_t status;

	if (outfile) {
		status = apr_file_open (&aprout, outfile, APR_READ | APR_WRITE | APR_CREATE,
				                APR_OS_DEFAULT, pool);
	} else {
		status = apr_file_open_stdout(&aprout, pool);
	}
	if (status) {
		IF_ERROR_RETURN (svn_error_wrap_apr(status, "Can't open output file"), pool, L);
	}

	if (errfile) {
		status = apr_file_open (&aprerr, errfile, APR_READ | APR_WRITE | APR_CREATE,
				                APR_OS_DEFAULT, pool);
	} else {
		status = apr_file_open_stderr(&aprerr, pool);
	}
	if (status) {
		IF_ERROR_RETURN (svn_error_wrap_apr(status, "Can't open error file"), pool , L);
	}

	apr_array_header_t *array;

	array = apr_array_make (pool, 0, sizeof (const char *));

	err = svn_client_diff3 (array, path1, &rev1, path2, &rev2,
			                TRUE, FALSE, FALSE, FALSE,
							APR_LOCALE_CHARSET, aprout, aprerr,
							ctx, pool);
	IF_ERROR_RETURN (err, pool, L);	

	svn_pool_destroy (pool);

	return 0;
}




/* Creates a file. 
 * Returns the new version of the file system */
static int
l_import (lua_State *L) {
	const char *path = luaL_checkstring (L, 1);
	const char *url = luaL_checkstring (L, 2);

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);
	url = svn_path_canonicalize(url, pool);

	svn_commit_info_t *commit_info;
	
	err = svn_client_import2 (&commit_info, path, url, FALSE, FALSE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushinteger (L, commit_info->revision);

	svn_pool_destroy (pool);
	
	return 1;
}


/* Gets the list of files in a directory. 
 * Returns this list indicating also in which version
 * a file was modified by the last time */
static int
l_list (lua_State *L) {
	const char *path = luaL_checkstring (L, 1);

	svn_opt_revision_t revision;
	svn_opt_revision_t peg_revision;

	revision.value.number =  lua_gettop (L) == 2 ? lua_tointeger (L, 2) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = get_revision_kind (path);
	}

	peg_revision = revision;

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

	apr_hash_t *entries;

	err = svn_client_ls3 (&entries, NULL, path, &peg_revision, &revision, TRUE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_newtable (L);

	apr_hash_index_t *hi;
	svn_dirent_t *val;

	for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi)) {
		
		char *name;

		apr_hash_this (hi, (void *) &name, NULL, (void *) &val);

		lua_pushboolean (L, 1);
		lua_setfield (L, -2, name);		
	}

	svn_pool_destroy (pool);

	return 1;
}


svn_error_t *
log_receiver (void *baton,
			  apr_hash_t *changed_paths,
			  svn_revnum_t revision,
			  const char *author,
			  const char *date,
			  const char *message,
			  apr_pool_t *pool) 
{

	lua_pushnumber ((lua_State*)baton, revision);
	
	lua_newtable ((lua_State*)baton);

	lua_pushstring ((lua_State*)baton, date);

	lua_setfield ((lua_State*)baton, -2, "date");

	lua_pushstring ((lua_State*)baton, message);

	lua_setfield ((lua_State*)baton, -2, "message");

	lua_pushstring ((lua_State*)baton, author);

	lua_setfield ((lua_State*)baton, -2, "author");

	lua_settable ((lua_State*)baton, -3);

	return NULL;
}


/* Gets the history of a file 
 * Returns a table with all revisions in which the file was modified and the
 * name of the file in that revision */
static int
l_log (lua_State *L) {

	const char *path = luaL_checkstring (L, 1);
	
	svn_opt_revision_t start, end;
	svn_opt_revision_t peg_revision;

	peg_revision.kind = svn_opt_revision_unspecified;
	end.kind = svn_opt_revision_head;
	start.kind = svn_opt_revision_number;

	start.value.number =  lua_gettop (L) == 2 ? lua_tointeger (L, 2) : 0;
	if (start.value.number) {
		start.kind = svn_opt_revision_number;
	} 

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

	apr_array_header_t *array;

	array = apr_array_make (pool, 1, sizeof (const char *));
	(*((const char **) apr_array_push (array))) = path;

	const int limit = 0;
	lua_newtable (L);

	err = svn_client_log3 (array, &peg_revision, &start, &end, limit, 
					FALSE, TRUE, log_receiver, L, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	svn_pool_destroy (pool);

	return 1;

}


static int
l_merge (lua_State *L) {
	const char *source1 = luaL_checkstring (L, 1);
	
	svn_opt_revision_t rev1;

	rev1.value.number = lua_tointeger (L, 2);
	if (rev1.value.number) {
		rev1.kind = svn_opt_revision_number;
	} else {
		rev1.kind = svn_opt_revision_head;
	}

	const char *source2 = luaL_checkstring (L, 3);

	svn_opt_revision_t rev2;

	rev2.value.number = lua_tointeger (L, 4);
	if (rev2.value.number) {
		rev2.kind = svn_opt_revision_number;
	} else {
		rev2.kind = svn_opt_revision_head;
	}

	const char *wcpath = luaL_checkstring (L, 5);

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);


	source1 = svn_path_canonicalize(source1, pool);
	source2 = svn_path_canonicalize(source2, pool);
	wcpath = svn_path_canonicalize(wcpath, pool);

	err = svn_client_merge2 (source1, &rev1, source2, &rev2, wcpath,
			TRUE, TRUE, FALSE, FALSE, NULL, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);	


	svn_pool_destroy (pool);

	return 0;
}





/* Creates a directory. 
 * Returns the new version of the file system */
static int
l_mkdir (lua_State *L) {
	
	const char *path = luaL_checkstring (L, 1);
	
	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

	svn_commit_info_t *commit_info;
	apr_array_header_t *array;

	array = apr_array_make (pool, 1, sizeof (const char *));
	(*((const char **) apr_array_push (array))) = path;

	err = svn_client_mkdir2 (&commit_info, array, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushinteger (L, commit_info->revision);

	svn_pool_destroy (pool);

	return 1;
}


static int
l_move (lua_State *L) {
	const char *src_path = luaL_checkstring (L, 1);
	const char *dest_path = luaL_checkstring (L, 2);

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	src_path = svn_path_canonicalize(src_path, pool);
	dest_path = svn_path_canonicalize(dest_path, pool);

	svn_commit_info_t *commit_info = NULL;

	err = svn_client_move4 (&commit_info, src_path, dest_path, FALSE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);	

	if (commit_info == NULL) {
		lua_pushnil (L);
	} else {
		lua_pushnumber (L, commit_info->revision);
	}

	svn_pool_destroy (pool);

	return 1;
}


static int
l_propget (lua_State *L) {

	const char *path = luaL_checkstring (L, 1);
	const char *propname = luaL_checkstring (L, 2);

	svn_opt_revision_t peg_revision;
	svn_opt_revision_t revision;

	revision.value.number =  lua_gettop (L) == 3 ? lua_tointeger (L, 3) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = svn_opt_revision_unspecified;
	}

	peg_revision = revision;

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

	apr_hash_t *props;

	err = svn_client_propget2 (&props, propname, path, &peg_revision, &revision, TRUE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_newtable (L);

	const void *key;
	void *val;
	apr_hash_index_t *hi;

	for (hi = apr_hash_first (pool, props); hi; hi = apr_hash_next (hi)) {
		apr_hash_this (hi, &key, NULL, &val);

		svn_string_t *s = (svn_string_t *) val;

		lua_pushstring (L, s->data);
		lua_setfield (L, -2, (char *) key);
	}

	svn_pool_destroy (pool);

	return 1;

}



static int
l_proplist (lua_State *L) {

	const char *path = luaL_checkstring (L, 1);

	svn_opt_revision_t peg_revision;
	svn_opt_revision_t revision;

	revision.value.number =  lua_gettop (L) == 2 ? lua_tointeger (L, 2) : 0;

	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = svn_opt_revision_unspecified;
	}	

	peg_revision = revision;

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

	apr_array_header_t *props;

	err = svn_client_proplist2 (&props, path, &peg_revision, &revision, TRUE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_newtable (L);

	int i;
	for (i = 0; i < props->nelts; ++i)
	{
		const void *key;
		void *val;
		svn_client_proplist_item_t *item = ((svn_client_proplist_item_t **)props->elts)[i];

		apr_hash_index_t *hi;
		for (hi = apr_hash_first (pool, item->prop_hash); hi; hi = apr_hash_next (hi)) {
			apr_hash_this (hi, &key, NULL, &val);

			svn_string_t *s = (svn_string_t *) val;

			lua_pushstring (L, s->data);
			lua_setfield (L, -2, (char *) key);
		}
	}

	svn_pool_destroy (pool);

	return 1;

}



static int
l_propset (lua_State *L) {

	const char *path = luaL_checkstring (L, 1);
	const char *prop = luaL_checkstring (L, 2);
	const char *value = lua_isnil (L, 3) ? 0 : luaL_checkstring (L, 3);
	
	svn_opt_revision_t revision;

	revision.value.number =  lua_gettop (L) == 4 ? lua_tointeger (L, 4) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = get_revision_kind (path);
	}

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

	if (value) {
		const svn_string_t sstring = {value, strlen (value) + 1};
		err = svn_client_propset2 (prop, &sstring, path, TRUE, FALSE, ctx, pool);
	} else {
		err = svn_client_propset2 (prop, 0, path, TRUE, FALSE, ctx, pool);
	}
	IF_ERROR_RETURN (err, pool, L);
	
	svn_pool_destroy (pool);

	return 0;

}






/* Creates a repository */
static int
l_repos_create (lua_State *L) {
	const char *path = luaL_checkstring (L, 1);
	svn_repos_t *repos_p;

	apr_pool_t *pool;

	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}

	path = svn_path_canonicalize(path, pool);
	
	svn_error_t *err;

	err = svn_repos_create (&repos_p, path, NULL, NULL, NULL, NULL, pool);
	IF_ERROR_RETURN (err, pool, L);

	return 0;
}


/* Deletes a repository */
static int
l_repos_delete (lua_State *L) {
	const char *path = luaL_checkstring (L, 1);

	apr_pool_t *pool;

	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}
	
	path = svn_path_canonicalize(path, pool);

	svn_error_t *err;

	err = svn_repos_delete (path, pool);
	IF_ERROR_RETURN (err, pool, L);

	return 0;
}


static int
l_revprop_get (lua_State *L) {

	const char *url = luaL_checkstring (L, 1);
	const char *propname = luaL_checkstring (L, 2);

	svn_opt_revision_t revision;

	revision.value.number =  lua_gettop (L) == 3 ? lua_tointeger (L, 3) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = get_revision_kind (url);
	}

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	url = svn_path_canonicalize(url, pool);

	svn_string_t *propval;
	svn_revnum_t rev;

	err = svn_client_revprop_get (propname, &propval, url, &revision, &rev, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushstring (L, propval->data);

	svn_pool_destroy (pool);

	return 1;

}


/* Gets the property list of a revision
 * Returns a table with all properties of a revision and
 * the associated values  */
static int
l_revprop_list (lua_State *L) {

	const char *path = luaL_checkstring (L, 1);
	
	svn_opt_revision_t revision;

	revision.value.number =  lua_gettop (L) == 2 ? lua_tointeger (L, 2) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = get_revision_kind (path);
	}

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

	apr_hash_t *entries;
	apr_hash_index_t *hi;
	void *val;
	const void *key;

	svn_revnum_t rev;
	err = svn_client_revprop_list (&entries, path, &revision, &rev, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_newtable (L);

	for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi)) {
		apr_hash_this (hi, &key, NULL, &val);
		
		svn_string_t *s = (svn_string_t *) val;

		lua_pushstring (L, s->data);
		lua_setfield (L, -2, (char *) key);
	}

	svn_pool_destroy (pool);

	return 1;

}

/* Changes the value of a property 
 * Returns true in case of success  */
static int
l_revprop_set (lua_State *L) {

	const char *path = luaL_checkstring (L, 1);
	const char *prop = luaL_checkstring (L, 2);
	const char *value = lua_isnil (L, 3) ? 0 : luaL_checkstring (L, 3);
	
	svn_opt_revision_t revision;

	revision.value.number =  lua_gettop (L) == 4 ? lua_tointeger (L, 4) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = get_revision_kind (path);
	}

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize(path, pool);

   	svn_revnum_t rev;	
	
	if (value) {
		const svn_string_t sstring = {value, strlen (value) + 1};
		err = svn_client_revprop_set (prop, &sstring, path, &revision, &rev, TRUE, ctx, pool);
	} else {
		err = svn_client_revprop_set (prop, 0, path, &revision, &rev, TRUE, ctx, pool);
	}
	IF_ERROR_RETURN (err, pool, L);
	
	svn_pool_destroy (pool);

	return 0;

}


static int
l_update (lua_State *L) {
	const char *path = luaL_checkstring (L, 1);

	svn_opt_revision_t revision;

	revision.value.number =  lua_gettop (L) == 2 ? lua_tointeger (L, 2) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = get_revision_kind (path);
	}

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	path = svn_path_canonicalize (path, pool);	

	apr_array_header_t *array;

	array = apr_array_make (pool, 1, sizeof (const char *));
	(*((const char **) apr_array_push (array))) = path;

	apr_array_header_t *result_revs = NULL;

	err = svn_client_update2 (&result_revs, array, &revision, TRUE, FALSE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);	

	if (result_revs == NULL) {
		lua_pushnil (L);
	} else {
		int rev = (int) ((int **) (result_revs->elts))[0];
		lua_pushnumber (L, rev);
	}

	svn_pool_destroy (pool);

	return 1;
}





static const struct luaL_Reg luasvn [] = {
	{"add", l_add},
	{"cat", l_cat},
	{"checkout", l_checkout},
	{"commit", l_commit},
	{"cleanup", l_cleanup},
	{"copy", l_copy},
	{"delete", l_delete},
	{"diff", l_diff},
	{"import", l_import},
	{"list", l_list},
	{"log", l_log},
	{"merge", l_merge},
	{"mkdir", l_mkdir},
	{"move", l_move},
	{"propget", l_propget},
	{"proplist", l_proplist},
	{"propset", l_propset},
	{"repos_create", l_repos_create},
	{"repos_delete", l_repos_delete},
	{"revprop_get", l_revprop_get},
	{"revprop_list", l_revprop_list},
	{"revprop_set", l_revprop_set},
	{"update", l_update},
	{NULL, NULL}
};

int
luaopen_luasvn (lua_State *L) {
	luaL_register (L, "luasvn", luasvn);
	return 1;
}

