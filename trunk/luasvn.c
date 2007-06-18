#include "svn_repos.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_client.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define BUFFER_SIZE 1000

#define IF_ERROR_RETURN(err, pool, L) do { \
	if (err) { \
		svn_pool_destroy (pool); \
		return send_error (L, err); \
	} \
} while (0)


static int
send_error (lua_State *L, svn_error_t *err) {
	lua_pushboolean (L, 0);
	
	lua_pushstring (L, err->message);

	svn_error_clear (err);
	
	return 2;
}

static int
init_pool (apr_pool_t **pool) {
	
	if (apr_initialize () != APR_SUCCESS) {
		return 1;
	}

	svn_dso_initialize ();

    *pool = svn_pool_create (NULL);

	return 0;
}

static int
init_pool_error (lua_State *L) {
	svn_error_t *err = malloc (sizeof (svn_error_t *));
	err->message = "Error initializing the memory pool\n";
	return send_error (L, err);
}

static svn_error_t *
init_fs_root_youngest (const char *repos_path, svn_repos_t **repos, svn_fs_t **fs, svn_revnum_t *youngest_rev,
		svn_fs_txn_t **txn, svn_fs_root_t **txn_root, apr_pool_t *pool) {

	svn_error_t *err;

	err = svn_fs_initialize (pool);
	if (err)
		return err;

	err = svn_repos_open(repos, repos_path, pool);
	if (err)
		return err;

	*fs = svn_repos_fs (*repos);

  	err = svn_fs_youngest_rev(youngest_rev, *fs, pool);
	if (err)
		return err;
  
	err = svn_fs_begin_txn2 (txn, *fs, *youngest_rev, 0, pool);
	if (err)
		return err;

	err = svn_fs_txn_root(txn_root, *txn, pool);
	return err;
}

static svn_error_t *
init_fs_root_rev (const char *repos_path, svn_repos_t **repos, svn_fs_t **fs, svn_revnum_t rev,
		svn_fs_txn_t **txn, svn_fs_root_t **txn_root, apr_pool_t *pool) {

	svn_error_t *err;

	err = svn_fs_initialize (pool);
	if (err)
		return err;

	err = svn_repos_open(repos, repos_path, pool);
	if (err)
		return err;

	*fs = svn_repos_fs (*repos);

	err = svn_fs_begin_txn2 (txn, *fs, rev, 0, pool);
	if (err)
		return err;

	err = svn_fs_txn_root(txn_root, *txn, pool);
	return err;
}




static int
l_create_dir (lua_State *L) {
	
	const char *repos_path = luaL_checkstring (L, 1);
	const char *new_directory = luaL_checkstring (L, 2);

	apr_pool_t *pool;

	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}

	
	svn_error_t *err;
	svn_repos_t *repos;
	svn_fs_t *fs;
	svn_revnum_t youngest_rev;
	svn_fs_txn_t *txn;
	svn_fs_root_t *txn_root;
	const char *conflict_str;


	err = init_fs_root_youngest (repos_path, &repos, &fs, &youngest_rev, &txn, &txn_root, pool);
	IF_ERROR_RETURN (err, pool, L);
 
	err = svn_fs_make_dir(txn_root, new_directory, pool);
	IF_ERROR_RETURN (err, pool, L);

	err = svn_repos_fs_commit_txn(&conflict_str, repos, &youngest_rev, txn, pool);
	IF_ERROR_RETURN (err, pool, L);
  
	lua_pushboolean (L, 1);
	lua_pushstring (L, lua_pushfstring (L,
				"Directory '%s' was successfully added as new revision '%d'.",
				new_directory, youngest_rev));

	svn_pool_destroy (pool);

	return 2;
}

static int
l_create_file (lua_State *L) {
	const char *repos_path = luaL_checkstring (L, 1);
	const char *new_file = luaL_checkstring (L, 2);

	apr_pool_t *pool;


	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}

	svn_error_t *err;
  	svn_repos_t *repos;
	svn_fs_t *fs;
	svn_revnum_t youngest_rev;
	svn_fs_txn_t *txn;
	svn_fs_root_t *txn_root;
	const char *conflict_str;


	err = init_fs_root_youngest (repos_path, &repos, &fs, &youngest_rev, &txn, &txn_root, pool);
	IF_ERROR_RETURN (err, pool, L);
  
	err = svn_fs_make_file(txn_root, new_file, pool);
	IF_ERROR_RETURN (err, pool, L);

	err = svn_repos_fs_commit_txn(&conflict_str, repos, &youngest_rev, txn, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushboolean (L, 1);
	lua_pushstring (L, lua_pushfstring (L,
				"File '%s' was successfully added as new revision '%d'.",
				new_file, youngest_rev));


	svn_pool_destroy (pool);
	
	return 2;
}


static int
l_change_file (lua_State *L) {
	const char *repos_path = luaL_checkstring (L, 1);
	const char *file = luaL_checkstring (L, 2);
	const char *new_text = luaL_checkstring (L, 3);

	apr_pool_t *pool;

	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}
	
	svn_error_t *err;
	svn_repos_t *repos;
	svn_fs_t *fs;
	svn_revnum_t youngest_rev;
	svn_fs_txn_t *txn;
	svn_fs_root_t *txn_root;
	svn_stream_t *stream;
	const char *conflict_str;


	err = init_fs_root_youngest (repos_path, &repos, &fs, &youngest_rev, &txn, &txn_root, pool);
	IF_ERROR_RETURN (err, pool, L);

	stream = svn_stream_empty (pool);
	
	err = svn_fs_apply_text (&stream, txn_root, file, NULL, pool);
	IF_ERROR_RETURN (err, pool, L);


	err = svn_stream_printf (stream, pool, "%s", new_text);
	IF_ERROR_RETURN (err, pool, L);
	
	svn_stream_close (stream);
	IF_ERROR_RETURN (err, pool, L);
	
	err = svn_repos_fs_commit_txn(&conflict_str, repos, 
			&youngest_rev, txn, pool);

	
	IF_ERROR_RETURN (err, pool, L);

	lua_pushboolean (L, 1);
	lua_pushstring (L, lua_pushfstring (L,
				"File '%s' was successfully changed as new revision '%d'.",
				file, youngest_rev));


	svn_pool_destroy (pool);

	return 2;
}

static int
l_get_file_content (lua_State *L) {
	const char *repos_path = luaL_checkstring (L, 1);
	const char *file = luaL_checkstring (L, 2);

	/* If can not convert to a integer, "rev" will receive zero */
	svn_revnum_t rev = lua_tointeger (L, 3);

	apr_pool_t *pool;


	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}
	
	svn_error_t *err;
	svn_repos_t *repos;
	svn_fs_t *fs;
	svn_fs_txn_t *txn;
	svn_fs_root_t *txn_root;
	svn_stream_t *stream;
	const char *conflict_str;

	if (rev) {  /* a specific version */
		err = init_fs_root_rev (repos_path, &repos, &fs, rev, &txn, &txn_root, pool);
	} else {  /* the youngest version */
		svn_revnum_t youngest_rev;
		err = init_fs_root_youngest (repos_path, &repos, &fs, &youngest_rev, &txn, &txn_root, pool);
	}

	IF_ERROR_RETURN (err, pool, L);
  
	err = svn_fs_file_contents (&stream, txn_root, file, pool);
	IF_ERROR_RETURN (err, pool, L);

	char *buffer = malloc (BUFFER_SIZE);
	int currentSize;
	char tmp [BUFFER_SIZE];
	apr_size_t len = BUFFER_SIZE;
	
	err = svn_stream_read (stream, buffer, &len);
	IF_ERROR_RETURN (err, pool, L);

	currentSize = len;

	while (len == BUFFER_SIZE) {
		
		err = svn_stream_read (stream, tmp, &len);
		IF_ERROR_RETURN (err, pool, L);

		if (len == BUFFER_SIZE) {
			buffer = realloc (buffer, currentSize + len);
			memcpy (buffer + currentSize, tmp, len);
			currentSize += len;
		} else {
			buffer = realloc (buffer, currentSize + len + 1);
			memcpy (buffer + currentSize, tmp, len);
			currentSize += len;
		}
	}

	err = svn_stream_close (stream);
	IF_ERROR_RETURN (err, pool, L);

	buffer [currentSize] = '\0';
	currentSize++;

	lua_pushboolean (L, 1);
	lua_pushstring (L, buffer);

	svn_pool_destroy (pool);

	return 2;
}



static int
l_get_files (lua_State *L) {
	const char *repos_path = luaL_checkstring (L, 1);
	const char *dir = luaL_checkstring (L, 2);
	
	apr_pool_t *pool;


	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}
	
	svn_error_t *err;
	svn_repos_t *repos;
	svn_fs_t *fs;
	svn_revnum_t youngest_rev;
	svn_fs_txn_t *txn;
	svn_fs_root_t *txn_root;


	err = init_fs_root_youngest (repos_path, &repos, &fs, &youngest_rev, &txn, &txn_root, pool);
	IF_ERROR_RETURN (err, pool, L);
  
	apr_hash_t *entries;

	err = svn_fs_dir_entries (&entries, txn_root, dir, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushboolean (L, 1);
	lua_newtable (L);

	apr_hash_index_t *hi;
	svn_fs_dirent_t *dirent;
	void *val;
	int j = 1;
	svn_revnum_t revision;


	for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi)) {
		apr_hash_this (hi, NULL, NULL, &val);
		dirent = (svn_fs_dirent_t *) val;

		char *tmp;
		tmp = malloc (strlen (dir) + 1 + strlen (dirent->name) + 1);
		strcpy (tmp, dir);
		strcat (tmp, "/");
		strcat (tmp, dirent->name);

		err = svn_fs_node_created_rev (&revision, txn_root, tmp, pool);
		IF_ERROR_RETURN (err, pool, L);

		lua_pushnumber (L, j++);
		
		lua_newtable (L);
		
		lua_pushstring (L, dirent->name);
		lua_setfield (L, -2, "name");

		lua_pushnumber (L, revision);
		lua_setfield (L, -2, "revision");
		
		lua_settable (L, -3);

	}


	svn_pool_destroy (pool);

	return 2;
}


static int
l_get_file_history (lua_State *L) {

	const char *repos_path = luaL_checkstring (L, 1);
	const char *file = luaL_checkstring (L, 2);
	
	apr_pool_t *pool;


	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}
	
	svn_error_t *err;
	svn_repos_t *repos;
	svn_fs_t *fs;
	svn_revnum_t youngest_rev;
	svn_fs_txn_t *txn;
	svn_fs_root_t *txn_root;


	/* In this case, init_fs_root_youngest does more than what you need, because a transaction
	 * node is initialized and what we really want it is a revision node
	 */
	err = init_fs_root_youngest (repos_path, &repos, &fs, &youngest_rev, &txn, &txn_root, pool);
	IF_ERROR_RETURN (err, pool, L);
  
	svn_fs_root_t *rev_root;
 
	/* Creates a revision node using the variables that have already been initialized by init_fs_root_youngest	
	 */
	err = svn_fs_revision_root (&rev_root, fs, youngest_rev, pool);
	IF_ERROR_RETURN (err, pool, L);

	svn_fs_history_t *history;

	err = svn_fs_node_history (&history, rev_root, file, pool);
	IF_ERROR_RETURN (err, pool, L);

	const char *tmp;
	svn_revnum_t rev;
	lua_pushboolean (L, 1);
	lua_newtable (L);
	int j = 1;

	err = svn_fs_history_prev (&history, history, FALSE, pool);
	IF_ERROR_RETURN (err, pool, L);
	
	while (history != NULL) {
		err = svn_fs_history_location (&tmp, &rev, history, pool);
		IF_ERROR_RETURN (err, pool, L);

		lua_pushnumber (L, j++);
		
		lua_newtable (L);
		
		lua_pushstring (L, tmp);
		lua_setfield (L, -2, "name");

		lua_pushnumber (L, rev);
		lua_setfield (L, -2, "revision");

		lua_settable (L, -3);
		
		err = svn_fs_history_prev (&history, history, FALSE, pool);
		IF_ERROR_RETURN (err, pool, L);
	}

	svn_pool_destroy (pool);

	return 2;

}

static int
l_get_rev_proplist (lua_State *L) {

	const char *repos_path = luaL_checkstring (L, 1);
	if (!lua_isnumber (L, 2)) {
		svn_error_t tmp;
		tmp.message = "Invalid type for argument 2\n";
		return send_error (L, &tmp);
	}
	const svn_revnum_t revision = lua_tointeger (L, 2);

	apr_pool_t *pool;


	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}
	
	svn_error_t *err;
	svn_repos_t *repos;
	svn_fs_t *fs;
	svn_revnum_t youngest_rev;
	svn_fs_txn_t *txn;
	svn_fs_root_t *txn_root;

	err = init_fs_root_youngest (repos_path, &repos, &fs, &youngest_rev, &txn, &txn_root, pool);
	IF_ERROR_RETURN (err, pool, L);
  
	apr_hash_t *entries;
	apr_hash_index_t *hi;
	void *val;
	const void *key;

	err = svn_fs_revision_proplist (&entries, fs, revision, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushboolean (L, 1);
	lua_newtable (L);
	int j = 1;

	for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi)) {
		apr_hash_this (hi, &key, NULL, &val);
		
		svn_string_t *s = (svn_string_t *) val;

		lua_pushnumber (L, j++);
		
		lua_newtable (L);
	
		lua_pushstring (L, (char *) key);
		lua_setfield (L, -2, "prop");

		lua_pushstring (L, s->data);
		lua_setfield (L, -2, "value");
	
		lua_settable (L, -3);

	}

	svn_pool_destroy (pool);

	return 2;

}

static int
l_change_rev_prop (lua_State *L) {

	const char *repos_path = luaL_checkstring (L, 1);
	const char *prop = luaL_checkstring (L, 2);
	const char *value = luaL_checkstring (L, 3);
	
	/* If can not convert to a integer, "rev" will receive zero */
	svn_revnum_t revision = lua_tointeger (L, 4);


	apr_pool_t *pool;


	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}
	
	svn_error_t *err;
	svn_repos_t *repos;
	svn_fs_t *fs;
	svn_fs_txn_t *txn;
	svn_fs_root_t *txn_root;

	if (revision == 0) {
		err = init_fs_root_youngest (repos_path, &repos, &fs, &revision, &txn, &txn_root, pool);
	} else {
		err = init_fs_root_rev (repos_path, &repos, &fs, revision, &txn, &txn_root, pool);
	}
	IF_ERROR_RETURN (err, pool, L);
  
	const svn_string_t sstring = {value, strlen (value) + 1};
	err = svn_fs_change_rev_prop (fs, revision, prop, &sstring, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushboolean (L, 1);
	lua_pushstring (L, lua_pushfstring (L, "Property successfully changed\n"));


	svn_pool_destroy (pool);

	return 2;

}



static int
l_file_exists (lua_State *L) {

	const char *repos_path = luaL_checkstring (L, 1);
	const char *file = luaL_checkstring (L, 2);

	apr_pool_t *pool;

	if (init_pool (&pool) != 0) {
		return init_pool_error (L);
	}
	
	svn_error_t *err;
	svn_repos_t *repos;
	svn_fs_t *fs;
	svn_revnum_t youngest_rev;
	svn_fs_txn_t *txn;
	svn_fs_root_t *txn_root;

	err = init_fs_root_youngest (repos_path, &repos, &fs, &youngest_rev, &txn, &txn_root, pool);
	IF_ERROR_RETURN (err, pool, L);
  
	svn_boolean_t res;

	err = svn_fs_is_file (&res, txn_root, file, pool);
	IF_ERROR_RETURN (err, pool, L);

	lua_pushboolean (L, 1);
	lua_pushboolean (L, res);

	svn_pool_destroy (pool);

	return 2;

}



static const struct luaL_Reg luasvn [] = {
	{"create_dir", l_create_dir},
	{"create_file", l_create_file},
	{"change_file", l_change_file},
	{"get_file_content", l_get_file_content},
	{"get_files", l_get_files},
	{"get_file_history", l_get_file_history},
	{"get_rev_proplist", l_get_rev_proplist},
	{"change_rev_prop", l_change_rev_prop},
	{"file_exists", l_file_exists},
	{NULL, NULL}
};

int
luaopen_luasvn (lua_State *L) {
	luaL_register (L, "luasvn", luasvn);
	return 1;
}

