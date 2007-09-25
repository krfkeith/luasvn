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


/* Creates a repository */
static int
l_create_repos (lua_State *L) {
	const char *repos_path = luaL_checkstring (L, 1);
	svn_repos_t *repos_p;

	apr_pool_t *pool;

	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}

	svn_error_t *err;

	err = svn_repos_create (&repos_p, repos_path, NULL, NULL, NULL, NULL, pool);
	IF_ERROR_RETURN (err, pool, L);

	return 0;
}


/* Deletes a repository */
static int
l_delete_repos (lua_State *L) {
	const char *repos_path = luaL_checkstring (L, 1);

	apr_pool_t *pool;

	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}

	svn_error_t *err;

	err = svn_repos_delete (repos_path, pool);
	IF_ERROR_RETURN (err, pool, L);

	return 0;
}



/* Creates a directory. 
 * Returns the new version of the file system */
static int
l_create_dir (lua_State *L) {
	
	const char *repos_path = luaL_checkstring (L, 1);
	const char *new_directory = luaL_checkstring (L, 2);
	
	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	svn_commit_info_t *commit_info;
	apr_array_header_t *array;

	char tmp [strlen(repos_path)+1+strlen(new_directory)+1];
	strcpy (tmp, repos_path);
	strcat (tmp, "/");
	strcat (tmp, new_directory);

	array = apr_array_make (pool, 1, sizeof (const char *));
	(*((const char **) apr_array_push (array))) = tmp;

	err = svn_client_mkdir2 (&commit_info, array, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushinteger (L, commit_info->revision);

	svn_pool_destroy (pool);

	return 1;
}

/* Creates a file. 
 * Returns the new version of the file system */
static int
l_create_file (lua_State *L) {
	const char *repos_path = luaL_checkstring (L, 1);
	const char *new_file = luaL_checkstring (L, 2);

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	svn_commit_info_t *commit_info;
	
	err = svn_client_import2 (&commit_info, new_file, repos_path, FALSE, FALSE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushinteger (L, commit_info->revision);

	svn_pool_destroy (pool);
	
	return 1;
}


svn_error_t *
write_fn (void *baton, const char *data, apr_size_t *len) {

	svn_stringbuf_appendbytes (baton, data, *len);

	return NULL;
}

/* Gets the content of a file 
 * Returns the content of a file */
static int
l_get_file_content (lua_State *L) {
	const char *repos_path = luaL_checkstring (L, 1);
	const char *file = luaL_checkstring (L, 2);

	svn_opt_revision_t peg_revision;
	svn_opt_revision_t revision;

	peg_revision.kind = svn_opt_revision_unspecified;

	revision.value.number =  lua_gettop (L) == 3 ? lua_tointeger (L, 3) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = svn_opt_revision_head;
	}

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	svn_stream_t *stream;

	stream = svn_stream_empty (pool);
	svn_stream_set_write (stream, write_fn);

	svn_stringbuf_t *buffer = svn_stringbuf_create ("\0", pool);

	svn_stream_set_baton (stream, buffer);

	err = svn_client_cat2 (stream, repos_path, &peg_revision, &revision, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushstring (L, buffer->data);

	svn_pool_destroy (pool);

	return 1;
}


static int
l_commit (lua_State *L) {
	const char *repos_path = luaL_checkstring (L, 1);
	const char *file = luaL_checkstring (L, 2);

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	apr_array_header_t *array;

	char tmp [strlen(repos_path)+1+strlen(file)+1];
	strcpy (tmp, repos_path);
	strcat (tmp, "/");
	strcpy (tmp, file);

	array = apr_array_make (pool, 1, sizeof (const char *));
	(*((const char **) apr_array_push (array))) = tmp;

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
l_checkout (lua_State *L) {
	const char *repos_path = luaL_checkstring (L, 1);
	const char *dir = luaL_checkstring (L, 2);

	svn_opt_revision_t revision;
	svn_opt_revision_t peg_revision;

	peg_revision.kind = svn_opt_revision_unspecified;

	revision.value.number =  lua_gettop (L) == 3 ? lua_tointeger (L, 3) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = svn_opt_revision_head;
	}


	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	svn_revnum_t rev;

	printf ("vivo em checkout\n");
	const char *true_url;
	err = svn_opt_parse_path(&peg_revision, &true_url, repos_path, pool);
	IF_ERROR_RETURN (err, pool, L);

	true_url = svn_path_canonicalize(true_url, pool);

	printf ("true = %s\n", true_url);

	dir = svn_path_basename(dir, pool);
    dir = svn_path_uri_decode(dir, pool);

	err = svn_client_checkout2 (&rev, repos_path, dir, &peg_revision, &revision, TRUE, FALSE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);
	
	lua_pushnumber (L, rev);

	svn_pool_destroy (pool);

	return 1;
}




/* Gets the list of files in a directory. 
 * Returns this list indicating also in which version
 * a file was modified by the last time */
static int
l_get_files (lua_State *L) {
	const char *repos_path = luaL_checkstring (L, 1);
	const char *dir = luaL_checkstring (L, 2);

	svn_opt_revision_t revision;
	svn_opt_revision_t peg_revision;

	peg_revision.kind = svn_opt_revision_unspecified;

	revision.value.number =  lua_gettop (L) == 3 ? lua_tointeger (L, 3) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = svn_opt_revision_head;
	}


	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	apr_hash_t *entries;

	err = svn_client_ls3 (&entries, NULL, repos_path, &peg_revision, &revision, TRUE, ctx, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_newtable (L);

	apr_hash_index_t *hi;
	svn_dirent_t *val;

	for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi)) {
		
		char *name;

		apr_hash_this (hi, (void *) &name, NULL, (void *) &val);

		/*svn_revnum_t revision;
		err = svn_fs_node_created_rev (&revision, txn_root, name, pool);
		IF_ERROR_RETURN (err, pool, L);*/

		lua_pushnumber (L, 1);
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
		
	lua_pushstring ((lua_State*)baton, date);

	lua_settable ((lua_State*)baton, -3);

	return NULL;
}


/* Gets the history of a file 
 * Returns a table with all revisions in which the file was modified and the
 * name of the file in that revision */
static int
l_get_file_history (lua_State *L) {

	const char *repos_path = luaL_checkstring (L, 1);
	const char *file = luaL_checkstring (L, 2);
	
	svn_opt_revision_t start, end;
	svn_opt_revision_t peg_revision;

	peg_revision.kind = svn_opt_revision_unspecified;
	end.kind = svn_opt_revision_head;
	start.kind = svn_opt_revision_number;

	start.value.number =  lua_gettop (L) == 3 ? lua_tointeger (L, 3) : 0;
	if (start.value.number) {
		start.kind = svn_opt_revision_number;
	} 


	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	apr_array_header_t *array;

	char tmp [strlen(repos_path)+1+strlen(file)+1];
	strcpy (tmp, repos_path);
	strcat (tmp, "/");
	strcat (tmp, file);

	array = apr_array_make (pool, 1, sizeof (const char *));
	(*((const char **) apr_array_push (array))) = tmp;

	const int limit = 0;
	lua_newtable (L);

	svn_client_log3 (array, &peg_revision, &start, &end, limit, 
					FALSE, TRUE, log_receiver, L, ctx, pool);

	svn_pool_destroy (pool);

	return 1;

}

/* Gets the property list of a revision
 * Returns a table with all properties of a revision and
 * the associated values  */
static int
l_get_rev_proplist (lua_State *L) {

	const char *repos_path = luaL_checkstring (L, 1);
	
	svn_opt_revision_t revision;

	revision.value.number =  lua_gettop (L) == 2 ? lua_tointeger (L, 2) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = svn_opt_revision_head;
	}

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

	apr_hash_t *entries;
	apr_hash_index_t *hi;
	void *val;
	const void *key;

	svn_revnum_t rev;
	err = svn_client_revprop_list (&entries, repos_path, &revision, &rev, ctx, pool);
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
l_change_rev_prop (lua_State *L) {

	const char *repos_path = luaL_checkstring (L, 1);
	const char *prop = luaL_checkstring (L, 2);
	const char *value = lua_isnil (L, 3) ? 0 : luaL_checkstring (L, 3);
	
	svn_opt_revision_t revision;

	revision.value.number =  lua_gettop (L) == 4 ? lua_tointeger (L, 4) : 0;
	if (revision.value.number) {
		revision.kind = svn_opt_revision_number;
	} else {
		revision.kind = svn_opt_revision_head;
	}

	apr_pool_t *pool;
	svn_error_t *err;
	svn_client_ctx_t *ctx;

	init_function (&ctx, &pool, L);

   	svn_revnum_t rev;	
	
	if (value) {
		const svn_string_t sstring = {value, strlen (value) + 1};
		err = svn_client_revprop_set (prop, &sstring, repos_path, &revision, &rev, TRUE, ctx, pool);
	} else {
		err = svn_client_revprop_set (prop, 0, repos_path, &revision, &rev, TRUE, ctx, pool);
	}
	IF_ERROR_RETURN (err, pool, L);
	
	lua_pushboolean (L, rev);

	svn_pool_destroy (pool);

	return 1;

}



static const struct luaL_Reg luasvn [] = {
	{"checkout", l_checkout},
	{"change_rev_prop", l_change_rev_prop},
	{"commit", l_commit},
	{"create_dir", l_create_dir},
	{"create_file", l_create_file},
	{"create_repos", l_create_repos},
	{"delete_repos", l_delete_repos},
	{"get_file_content", l_get_file_content},
	{"get_file_history", l_get_file_history},
	{"get_files", l_get_files},
	{"get_rev_proplist", l_get_rev_proplist},
	{NULL, NULL}
};

int
luaopen_luasvn (lua_State *L) {
	luaL_register (L, "luasvn", luasvn);
	return 1;
}

