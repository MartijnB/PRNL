// BEGIN INCLUDED FILES
/* The difference between zend_symtable_X and zend_hash_X is that
 * the _symtable version will check if the key is a string of an
 * integer, and if so, use the int version instead. We can use the
 * zend_hash_X versions safely for symbol tables, since
 * variables cant be integer strings, but we cant safely use
 * zend_hash_X versions for hashtable lookups. Well-named, they are.
 */

/* An important note of zval*s and zval**s. Frequently, zval** are
 * fetched from arrays and symbol tables. The zval** will point into
 * the array, and so updating it will update the relevant array
 * entry. It is normally not the same to dereference it to a local
 * variable, and pass a reference to that.
 */



// Some common functions
#include "php.h"

/*
 * The runtime needs its own initialization and finalization.  phc is
 * responsible for generating calls to these functions.
 */

void
init_runtime ()
{
}

void
finalize_runtime ()
{
}

static void
ht_debug (HashTable * ht)
{
  printf ("\nHASH\n");
  if (ht == NULL)
    {
      printf ("NULL\n");
      return;
    }
  for (zend_hash_internal_pointer_reset (ht);
       zend_hash_has_more_elements (ht) == SUCCESS;
       zend_hash_move_forward (ht))
    {
      char *key;
      unsigned keylen;
      unsigned long idx;
      int type;
      zval **ppzval;
      zval *zvp;

      type = zend_hash_get_current_key_ex (ht, &key, &keylen, &idx, 0, NULL);
      zend_hash_get_current_data (ht, (void **) &ppzval);

      zvp = *ppzval;

      if (type == HASH_KEY_IS_STRING)
	{
	  printf ("%s", key);
	}
      else
	{
	  printf ("%ld", idx);
	}

      printf (": addr = %08lX, refcount = %d, is_ref = %d ",
	      (long unsigned int) (*ppzval), (*ppzval)->refcount,
	      (*ppzval)->is_ref);
      switch (Z_TYPE_P (zvp))
	{
	case IS_NULL:
	  printf ("(NULL)");
	  break;
	case IS_LONG:
	  printf ("(%ldL)", Z_LVAL_P (zvp));
	  break;
	case IS_DOUBLE:
	  printf ("(%lff)", Z_DVAL_P (zvp));
	  break;
	case IS_BOOL:
	  printf (Z_BVAL_P (zvp) ? "(true)" : "(false)");
	  break;
	case IS_ARRAY:
	  printf ("(array(%d))", Z_ARRVAL_P (zvp)->nNumOfElements);
	  break;
	case IS_OBJECT:
	  printf ("(Object)");
	  break;
	case IS_STRING:
	  printf ("(\"%s\")", Z_STRVAL_P (zvp));
	  break;
	case IS_RESOURCE:
	  printf ("(Resource)");
	  break;
	default:
	  printf ("(Invalid: %d)", Z_TYPE_P (zvp));
	}

      printf ("\n");

    }
  printf ("END HASH\n");
}

// Call ht_debug on the named var in the given symbol table
static void
ht_var_debug (HashTable * st, char *name)
{
  zval **p_zvp;
  if (zend_symtable_find (st, name, strlen (name) + 1,
			  (void **) &p_zvp) != SUCCESS)
    {
      printf ("VAR NOT IN SYMBOL TABLE: '%s'\n", name);
      return;
    }

  if (Z_TYPE_P (*p_zvp) != IS_ARRAY)
    {
      printf ("NOT HASH\n");
      return;
    }

  ht_debug ((*p_zvp)->value.ht);
}

static zval* counters;

static void init_counters ()
{
  ALLOC_INIT_ZVAL (counters);
  array_init (counters);
}

// Dump and cleanup memory
static void finalize_counters ()
{
  HashTable* ht = Z_ARRVAL_P (counters);
  for (zend_hash_internal_pointer_reset (ht);
       zend_hash_has_more_elements (ht) == SUCCESS;
       zend_hash_move_forward (ht))
    {
      char *key;
      zval **p_zvp;

      zend_hash_get_current_key_ex (ht, &key, NULL, NULL, 0, NULL);
      zend_hash_get_current_data (ht, (void **) &p_zvp);

      fprintf (stderr, "COUNTER:%s:%ld\n", key, Z_LVAL_P (*p_zvp));
    }

  zval_ptr_dtor (&counters);
}

static void increment_counter (char* name, int length, ulong hashval)
{
  zval** p_zvp;
  int success = zend_hash_quick_find (Z_ARRVAL_P (counters),
				      name,
				      length,
				      hashval,
				      (void **) &p_zvp);

  if (success == SUCCESS)
    {
      Z_LVAL_PP (p_zvp)++;
    }
  else
    {

      zval* new_val;
      ALLOC_INIT_ZVAL (new_val);
      ZVAL_LONG (new_val, 1);

      zend_hash_quick_add (Z_ARRVAL_P (counters),
			   name,
			   length,
			   hashval,
			   &new_val,
			   sizeof (zval *),
			   NULL);
    }
}



/* Make a copy of *P_ZVP, storing it in *P_ZVP. */
static zval *
zvp_clone_ex (zval * zvp)
{
  // TODO: use INIT_PZVAL_COPY
  zval *clone;
  MAKE_STD_ZVAL (clone);
  clone->value = zvp->value;
  clone->type = zvp->type;
  zval_copy_ctor (clone);
  return clone;
}


static inline int
in_copy_on_write (zval * zvp)
{
  return (zvp->refcount > 1 && !zvp->is_ref);
}

static inline int
in_change_on_write (zval * zvp)
{
  return (zvp->refcount > 1 && zvp->is_ref);
}

/* If *P_ZVP is in a copy-on-write set, separate it by overwriting
 * *P_ZVP with a clone of itself, and lowering the refcount on the
 * original. */
static void
sep_copy_on_write (zval ** p_zvp)
{
  if (!in_copy_on_write (*p_zvp))
    return;

  zval *old = *p_zvp;

  *p_zvp = zvp_clone_ex (*p_zvp);

  zval_ptr_dtor (&old);
}

/* If *P_ZVP is in a copy-on-write set, separate it by overwriting
 * *P_ZVP with a clone of itself, and lowering the refcount on the
 * original. */
static void
sep_change_on_write (zval ** p_zvp)
{
  assert (in_change_on_write (*p_zvp));

  zval *old = *p_zvp;

  *p_zvp = zvp_clone_ex (*p_zvp);

  zval_ptr_dtor (&old);
}

/* Assign RHS into LHS, by reference. After this, LHS will point to the same
 * zval* as RHS. */
static void
copy_into_ref (zval ** lhs, zval ** rhs)
{
  (*rhs)->is_ref = 1;
  (*rhs)->refcount++;
  zval_ptr_dtor (lhs);
  *lhs = *rhs;
}


// Overwrite one zval with another
static void
overwrite_lhs (zval * lhs, zval * rhs)
{
  // First, call the destructor to remove any data structures
  // associated with lhs that will now be overwritten
  zval_dtor (lhs);
  // Overwrite LHS
  lhs->value = rhs->value;
  lhs->type = rhs->type;
  zval_copy_ctor (lhs);
}

// Overwrite one zval with another
static void
overwrite_lhs_no_copy (zval * lhs, zval * rhs)
{
  // First, call the destructor to remove any data structures
  // associated with lhs that will now be overwritten
  zval_dtor (lhs);
  // Overwrite LHS
  lhs->value = rhs->value;
  lhs->type = rhs->type;
}

/* Write P_RHS into the symbol table as a variable named VAR_NAME. */
// NOTE: We do not alter p_rhs's refcount, unless p_lhs joins its
// Copy-on-write set.
// TODO: this is crying out to be inlined.
static void
write_var (zval ** p_lhs, zval * rhs)
{
  if (!(*p_lhs)->is_ref)
    {
      zval_ptr_dtor (p_lhs);
      // Take a copy of RHS for LHS.
      if (rhs->is_ref)
	{
	  *p_lhs = zvp_clone_ex (rhs);
	}
      else			// share a copy
	{
	  rhs->refcount++;
	  *p_lhs = rhs;
	}
    }
  else
    {
      overwrite_lhs (*p_lhs, rhs);
    }
}

// TODO: this functino does too much, and much might be redundant
static zval **
get_st_entry (HashTable * st, char *name, int length, ulong hashval TSRMLS_DC)
{
  zval **p_zvp;
  if (zend_hash_quick_find
      (st, name, length, hashval, (void **) &p_zvp) == SUCCESS)
    {
      assert (p_zvp != NULL);
      return p_zvp;
    }

  // If we dont find it, put EG (uninitialized_zval_ptr) into the
  // hashtable, and return a pointer to its container.
  EG (uninitialized_zval_ptr)->refcount++;
  int result = zend_hash_quick_add (st, name, length, hashval,
				    &EG (uninitialized_zval_ptr),
				    sizeof (zval *), (void **) &p_zvp);
  assert (result == SUCCESS);
  assert (p_zvp != NULL);

  return p_zvp;
}

/* Read the variable named VAR_NAME from the local symbol table and
 * return it. If the variable doent exist, a new one is created and
 * *IS_NEW is set.  */
static zval *
read_var (HashTable * st, char *name, int length, ulong hashval TSRMLS_DC)
{
  zval **p_zvp;
  if (zend_hash_quick_find
      (st, name, length, hashval, (void **) &p_zvp) == SUCCESS)
    return *p_zvp;

  return EG (uninitialized_zval_ptr);
}

static long
get_integer_index (zval * ind TSRMLS_DC)
{
  long index;
  switch (Z_TYPE_P (ind))
    {
    case IS_DOUBLE:
      return (long) Z_DVAL_P (ind);

    case IS_LONG:
    case IS_BOOL:
      return Z_LVAL_P (ind);

    case IS_NULL:
      return 0;

    default:
      php_error_docref (NULL TSRMLS_CC, E_WARNING, "Illegal offset type");
    }
}

static zval *
read_string_index (zval * var, zval * ind TSRMLS_DC)
{
  // This must always allocate memory, since we cant return the
  // passed string.
  assert (Z_TYPE_P (var) == IS_STRING);
  long index = get_integer_index (ind TSRMLS_CC);

  zval *result;
  ALLOC_INIT_ZVAL (result);

  if (index >= Z_STRLEN_P (var) || index < 0)
    {
      // this is 1 byte long, must be copied
      ZVAL_STRINGL (result, "", 0, 1);
    }
  else
    {
      char *string = Z_STRVAL_P (var);
      ZVAL_STRINGL (result, &string[index], 1, 1);
    }

  return result;
}

/* Given a string (p_lhs), write into it for $x[i] = $y; */
void
write_string_index (zval ** p_lhs, zval * ind, zval * rhs TSRMLS_DC)
{
  assert (Z_TYPE_P (*p_lhs) == IS_STRING);

  long index = get_integer_index (ind TSRMLS_CC);

  // Get the appropriate character
  char new_char;
  if (Z_TYPE_P (rhs) != IS_STRING)
    {
      // TODO: remove allocate
      zval *copy = zvp_clone_ex (rhs);
      convert_to_string (copy);
      new_char = Z_STRVAL_P (copy)[0];
      zval_ptr_dtor (&copy);
    }
  else
    {
      new_char = Z_STRVAL_P (rhs)[0];
    }

  // Bounds check
  if (index < 0)
    {
      php_error_docref (NULL TSRMLS_CC, E_WARNING,
			"Illegal string offset:  %ld", index);
      return;
    }

  // We overwrite if it's change-on-write
  sep_copy_on_write (p_lhs);

  if (index > Z_STRLEN_PP (p_lhs))
    {
      // Extend to fix new
      int len = Z_STRLEN_PP (p_lhs);
      int new_length = index + 1;	// space for the new character
      Z_STRVAL_PP (p_lhs) = erealloc (Z_STRVAL_PP (p_lhs), new_length + 1);

      // pad with ' '
      memset (&Z_STRVAL_PP (p_lhs)[len], ' ', index - len);

      // change the strlen
      Z_STRLEN_PP (p_lhs) = new_length;

      // add a null terminator
      Z_STRVAL_PP (p_lhs)[new_length] = '\0';
    }

  // write in the first character of the new value
  Z_STRVAL_PP (p_lhs)[index] = new_char;


  // index < 0: E_WARNING illegal string offset
}

// Extract the hashtable from a hash-valued zval
static HashTable *
extract_ht_ex (zval * arr TSRMLS_DC)
{
  // TODO: this likely should be inlined somewhere.
  assert (!in_copy_on_write (arr));
  if (Z_TYPE_P (arr) == IS_NULL)
    {
      array_init (arr);
    }
  else if (Z_TYPE_P (arr) != IS_ARRAY)
    {
      php_error_docref (NULL TSRMLS_CC, E_WARNING,
			"Cannot use a scalar value as an array");
      array_init (arr);
    }
  return Z_ARRVAL_P (arr);
}


/* P_VAR points into a symbol table, at a variable which we wish to index as a hashtable. */
static HashTable *
extract_ht (zval ** p_var TSRMLS_DC)
{
  sep_copy_on_write (p_var);

  return extract_ht_ex (*p_var TSRMLS_CC);
}

/* Using IND as a key to HT, call the appropriate zend_index_X
 * function with data as a parameter, and return its result. This
 * updates the zval** pointed to by DATA. */
static int
ht_find (HashTable * ht, zval * ind, zval *** data)
{
  int result;
  if (Z_TYPE_P (ind) == IS_LONG || Z_TYPE_P (ind) == IS_BOOL)
    {
      result = zend_hash_index_find (ht, Z_LVAL_P (ind), (void **) data);
    }
  else if (Z_TYPE_P (ind) == IS_DOUBLE)
    {
      result = zend_hash_index_find (ht, (long) Z_DVAL_P (ind),
				     (void **) data);
    }
  else if (Z_TYPE_P (ind) == IS_NULL)
    {
      result = zend_hash_find (ht, "", sizeof (""), (void **)data);
    }
  else if (Z_TYPE_P (ind) == IS_STRING)
    {
      result = zend_symtable_find (ht, Z_STRVAL_P (ind),
				   Z_STRLEN_P (ind) + 1, (void **) data);
    }
  else
    {
      // TODO: I believe this might need a warning.

      // TODO avoid alloc
      // use a string index for other types
      zval *string_index;
      MAKE_STD_ZVAL (string_index);
      string_index->value = ind->value;
      string_index->type = ind->type;
      zval_copy_ctor (string_index);
      convert_to_string (string_index);

      result = zend_symtable_find (ht, Z_STRVAL_P (string_index),
				   Z_STRLEN_P (string_index) + 1,
				   (void **) data);
      zval_ptr_dtor (&string_index);
    }
  return result;
}


static int
check_array_index_type (zval * ind TSRMLS_DC)
{
  if (Z_TYPE_P (ind) == IS_OBJECT || Z_TYPE_P (ind) == IS_ARRAY)
    {
      php_error_docref (NULL TSRMLS_CC, E_WARNING, "Illegal offset type");
      return 0;
    }

  return 1;
}

// Update a hashtable using a zval* index
static void
ht_update (HashTable * ht, zval * ind, zval * val, zval *** dest)
{
  int result;
  if (Z_TYPE_P (ind) == IS_LONG || Z_TYPE_P (ind) == IS_BOOL)
    {
      result = zend_hash_index_update (ht, Z_LVAL_P (ind), &val,
				       sizeof (zval *), (void **) dest);
    }
  else if (Z_TYPE_P (ind) == IS_DOUBLE)
    {
      result = zend_hash_index_update (ht, (long) Z_DVAL_P (ind),
				       &val, sizeof (zval *), (void **) dest);
    }
  else if (Z_TYPE_P (ind) == IS_NULL)
    {
      result = zend_hash_update (ht, "", sizeof (""), &val,
				 sizeof (zval *), (void **) dest);
    }
  else if (Z_TYPE_P (ind) == IS_STRING)
    {
      result = zend_symtable_update (ht, Z_STRVAL_P (ind),
				     Z_STRLEN_P (ind) + 1,
				     &val, sizeof (zval *), (void **) dest);
    }
  else
    {
      // TODO avoid alloc
      zval *string_index;
      MAKE_STD_ZVAL (string_index);
      string_index->value = ind->value;
      string_index->type = ind->type;
      zval_copy_ctor (string_index);
      convert_to_string (string_index);
      result = zend_symtable_update (ht, Z_STRVAL_P (string_index),
				     Z_STRLEN_P (string_index) + 1,
				     &val, sizeof (zval *), (void **) dest);

      zval_ptr_dtor (&string_index);
    }
  assert (result == SUCCESS);
}

// Delete from a hashtable using a zval* index
static void
ht_delete (HashTable * ht, zval * ind)
{
  // This may fail if the index doesnt exist, which is fine.
  if (Z_TYPE_P (ind) == IS_LONG || Z_TYPE_P (ind) == IS_BOOL)
    {
      zend_hash_index_del (ht, Z_LVAL_P (ind));
    }
  else if (Z_TYPE_P (ind) == IS_DOUBLE)
    {
      zend_hash_index_del (ht, (long) Z_DVAL_P (ind));
    }
  else if (Z_TYPE_P (ind) == IS_NULL)
    {
      zend_hash_del (ht, "", sizeof (""));
    }
  else if (Z_TYPE_P (ind) == IS_STRING)
    {
      zend_hash_del (ht, Z_STRVAL_P (ind), Z_STRLEN_P (ind) + 1);
    }
  else
    {
      // TODO avoid alloc
      zval *string_index;
      MAKE_STD_ZVAL (string_index);
      string_index->value = ind->value;
      string_index->type = ind->type;
      zval_copy_ctor (string_index);
      convert_to_string (string_index);
      zend_hash_del (ht, Z_STRVAL_P (string_index),
		     Z_STRLEN_P (string_index) + 1);

      zval_ptr_dtor (&string_index);
    }
}

// Check if a key exists in a hashtable 
static int
ht_exists (HashTable * ht, zval * ind)
{
  if (Z_TYPE_P (ind) == IS_LONG || Z_TYPE_P (ind) == IS_BOOL)
    {
      return zend_hash_index_exists (ht, Z_LVAL_P (ind));
    }
  else if (Z_TYPE_P (ind) == IS_DOUBLE)
    {
      return zend_hash_index_exists (ht, (long) Z_DVAL_P (ind));
    }
  else if (Z_TYPE_P (ind) == IS_NULL)
    {
      return zend_hash_exists (ht, "", sizeof (""));
    }
  else if (Z_TYPE_P (ind) == IS_STRING)
    {
      return zend_hash_exists (ht, Z_STRVAL_P (ind), Z_STRLEN_P (ind) + 1);
    }
  else
    {
      // TODO avoid alloc
      int result;
      zval *string_index;
      MAKE_STD_ZVAL (string_index);
      string_index->value = ind->value;
      string_index->type = ind->type;
      zval_copy_ctor (string_index);
      convert_to_string (string_index);
      result = zend_hash_exists (ht, Z_STRVAL_P (string_index),
				 Z_STRLEN_P (string_index) + 1);
      zval_ptr_dtor (&string_index);
      return result;
    }
  assert (0);
}

static zval **
get_ht_entry (zval ** p_var, zval * ind TSRMLS_DC)
{
  if (Z_TYPE_P (*p_var) == IS_STRING)
    {
      if (Z_STRLEN_PP (p_var) > 0)
	{
	  php_error_docref (NULL TSRMLS_CC, E_ERROR,
			    "Cannot create references to/from string offsets nor overloaded objects");
	}
    }

  if (Z_TYPE_P (*p_var) != IS_ARRAY)
    {
      zval_ptr_dtor (p_var);
      ALLOC_INIT_ZVAL (*p_var);
      array_init (*p_var);
    }

  HashTable *ht = extract_ht (p_var TSRMLS_CC);

  zval **data;
  if (ht_find (ht, ind, &data) == SUCCESS)
    {
      assert (data != NULL);
      return data;
    }

  // If we dont find it, put EG (uninitialized_zval_ptr) into the
  // hashtable, and return a pointer to its container.
  EG (uninitialized_zval_ptr)->refcount++;
  ht_update (ht, ind, EG (uninitialized_zval_ptr), &data);

  assert (data != NULL);

  return data;
}


// Like extract_ht_ex, but for objects 
static HashTable *
extract_field_ex (zval * obj TSRMLS_DC)
{
  // TODO: this likely should be inlined somewhere.
  assert (!in_copy_on_write (obj));
  if (Z_TYPE_P (obj) == IS_NULL)
    {
      assert (0);
      // TODO: implement initialization
    }
  else if (Z_TYPE_P (obj) != IS_OBJECT)
    {
      // TODO: test if this is the right error message
      php_error_docref (NULL TSRMLS_CC, E_WARNING,
			"Cannot use a scalar value as an object");
      // TODO: implement initialization
      assert (0);
    }
  return Z_OBJPROP_P (obj);
}

// Like extract_ht, but for objects
static HashTable *
extract_field (zval ** p_var TSRMLS_DC)
{
  sep_copy_on_write (p_var);

  return extract_field_ex (*p_var TSRMLS_CC);
}

// Like get_ht_entry, but for objects
static zval **
get_field (zval ** p_var, char *ind TSRMLS_DC)
{
  if (Z_TYPE_P (*p_var) != IS_OBJECT)
    {
      // TODO: implement initialization
      assert (0);
    }

  HashTable *ht = extract_field (p_var TSRMLS_CC);

  zval **data;
  if (zend_symtable_find (ht, ind, strlen (ind) + 1, (void **) &data) ==
      SUCCESS)
    {
      assert (data != NULL);
      return data;
    }

  // If we dont find it, put EG (uninitialized_zval_ptr) into the
  // hashtable, and return a pointer to its container.
  EG (uninitialized_zval_ptr)->refcount++;
  zend_symtable_update (ht, ind, strlen (ind) + 1,
			&EG (uninitialized_zval_ptr), sizeof (zval *),
			(void **) &data);

  assert (data != NULL);

  return data;
}

void
read_array (zval ** result, zval * array, zval * ind TSRMLS_DC)
{
  // Memory can be allocated in read_string_index
  if (array == EG (uninitialized_zval_ptr))
    {
      *result = array;
      return;
    }

  // Since we know its an array, and we dont write to it, we dont need
  // to separate it.
  HashTable *ht = Z_ARRVAL_P (array);

  // find the result
  zval **p_result;
  if (ht_find (ht, ind, &p_result) == SUCCESS)
    {
      *result = *p_result;
      return;
    }

  *result = EG (uninitialized_zval_ptr);
}

/* If its not an array, convert it into an array. */
static void
check_array_type (zval ** p_var TSRMLS_DC)
{
  if ((Z_TYPE_P (*p_var) == IS_BOOL && !Z_BVAL_PP (p_var))
      || Z_TYPE_P (*p_var) == IS_NULL
      || (Z_TYPE_P (*p_var) == IS_STRING && Z_STRLEN_PP (p_var) == 0))
    {
      // Non ref use new values
      if (!PZVAL_IS_REF (*p_var))
	{
	  zval_ptr_dtor (p_var);
	  ALLOC_INIT_ZVAL (*p_var);
	}
      else
	// Refs are just replaced
	zval_dtor (*p_var);

      array_init (*p_var);
    }
  else if (Z_TYPE_PP (p_var) != IS_STRING && Z_TYPE_PP (p_var) != IS_ARRAY)
    {
      // TODO: why are these different types than pushing
      php_error_docref (NULL TSRMLS_CC, E_WARNING,
			"Cannot use a scalar value as an array");
    }
}

/* If its not an array, convert it into an object. */
static void
check_object_type (zval ** p_var TSRMLS_DC)
{
  // TODO: implement
}

/* Push EG (uninitialized_zval_ptr) and return a pointer into the ht
 * for it */
/*
 * Converted to array automatically:
 *    ""
 *    NULL
 *    false
 *
 * Warning, no conversion:
 *    ints
 *    floats
 *    true
 *
 * Error, no conversion:
 *    strings other than ""
 */
// TODO: objects, resources, etc
static zval **
push_and_index_ht (zval ** p_var TSRMLS_DC)
{
  // Check for errors conditions

  if (Z_TYPE_P (*p_var) == IS_STRING && Z_STRLEN_PP (p_var) > 0)
    {
      php_error_docref (NULL TSRMLS_CC, E_ERROR,
			"[] operator not supported for strings");
      assert (0);		// unreachable
    }

  if (Z_TYPE_P (*p_var) == IS_BOOL && Z_BVAL_PP (p_var)
      || Z_TYPE_P (*p_var) == IS_LONG || Z_TYPE_P (*p_var) == IS_DOUBLE)
    {
      php_error_docref (NULL TSRMLS_CC, E_WARNING,
			"Cannot use a scalar value as an array");
      return NULL;
    }

  if (Z_TYPE_P (*p_var) != IS_ARRAY)
    {
      zval_ptr_dtor (p_var);
      ALLOC_INIT_ZVAL (*p_var);
      array_init (*p_var);
    }

  // if its not an array, make it an array
  HashTable *ht = extract_ht (p_var TSRMLS_CC);
  zval **data;

  EG (uninitialized_zval_ptr)->refcount++;
  int result = zend_hash_next_index_insert (ht, &EG (uninitialized_zval_ptr),
					    sizeof (zval *), (void **) &data);
  assert (result == SUCCESS);

  assert (data);

  return data;
}


/*
 * isset
 */
static int
isset_var (HashTable * st, char *name, int length)
{
  return zend_hash_exists (st, name, length);
}

static int
isset_array (zval ** p_var, zval * ind)
{
  if (Z_TYPE_P (*p_var) == IS_STRING)
    {
      ind = zvp_clone_ex (ind);
      convert_to_long (ind);
      int result = (Z_LVAL_P (ind) >= 0
		    && Z_LVAL_P (ind) < Z_STRLEN_PP (p_var));
      assert (ind->refcount == 1);
      zval_ptr_dtor (&ind);
      return result;
    }

  // NO error required; return false
  if (Z_TYPE_P (*p_var) != IS_ARRAY)
    return 0;

  // if its not an array, make it an array
  HashTable *ht = Z_ARRVAL_P (*p_var);

  zval **data;
  if (ht_find (ht, ind, &data) == SUCCESS)
    {
      return !ZVAL_IS_NULL (*data);
    }
  else
    return 0;
}


static zval **
fetch_var_arg_by_ref (zval ** p_arg)
{
  // We are passing by reference
  sep_copy_on_write (p_arg);

  // We don't need to restore ->is_ref afterwards,
  // because the called function will reduce the
  // refcount of arg on return, and will reset is_ref to
  // 0 when refcount drops to 1.  If the refcount does
  // not drop to 1 when the function returns, but we did
  // set is_ref to 1 here, that means that is_ref must
  // already have been 1 to start with (since if it had
  // not, that means that the variable would have been
  // in a copy-on-write set, and would have been
  // seperated above).
  (*p_arg)->is_ref = 1;

  return p_arg;
}

/* Dont pass-by-ref */
static zval *
fetch_var_arg (zval * arg, int *is_arg_new)
{
  if (arg->is_ref)
    {
      // We dont separate since we don't own one of ARG's references.
      arg = zvp_clone_ex (arg);
      *is_arg_new = 1;

      // It seems we get incorrect refcounts without this.
      // TODO This decreases the refcount to zero, which seems wrong,
      // but gives the right answer. We should look at how zend does
      // this.

      arg->refcount--;
    }
  return arg;
}

// TODO dont overwrite line numbers if we're compiling an extension
static void
phc_setup_error (int init, char *filename, int line_number,
		 zend_function * function TSRMLS_DC)
{
  static int old_in_compilation;
  static int old_in_execution;
  static char *old_filename;
  static int old_lineno;
  static zend_function *old_function;
  if (init)
    {
      if (filename == NULL)
	filename = "[phc_compiled_file]";
      // Save old values
      old_in_compilation = CG (in_compilation);
      old_in_execution = EG (in_execution);
      old_filename = CG (compiled_filename);
      old_lineno = CG (zend_lineno);
      old_function = EG (function_state_ptr)->function;
      // Put in our values
      CG (in_compilation) = 1;
      EG (in_execution) = 1;
      CG (compiled_filename) = filename;
      CG (zend_lineno) = line_number;
      if (function)
	EG (function_state_ptr)->function = function;
    }
  else
    {
      CG (in_compilation) = old_in_compilation;
      EG (in_execution) = old_in_execution;
      CG (compiled_filename) = old_filename;
      CG (zend_lineno) = old_lineno;
      EG (function_state_ptr)->function = old_function;
    }
}

static void
initialize_function_call (zend_fcall_info * fci, zend_fcall_info_cache * fcic,
			  char *function_name, char *filename,
			  int line_number TSRMLS_DC)
{
  if (fcic->initialized)
    return;

  zval fn;
  INIT_PZVAL (&fn);
  ZVAL_STRING (&fn, function_name, 0);
  int result = zend_fcall_info_init (&fn, fci, fcic TSRMLS_CC);
  if (result != SUCCESS)
    {
      phc_setup_error (1, filename, line_number, NULL TSRMLS_CC);
      php_error_docref (NULL TSRMLS_CC, E_ERROR,
			"Call to undefined function %s()", function_name);
    }
}

/*
 * Initialize zend_fcall_info for a method lookup
 *
 * Implementation partly based on zend_call_method in Zend/zend_interfaces.c
 * Main difference is that we use Z_OBJ_HTT_PP(obj)->get_method to retrieve
 * the function handler for the method instead of looking it up directly;
 * this means that we correctly deal with __call.
 */

static void
initialize_method_call (zend_fcall_info * fci, zend_fcall_info_cache * fcic,
			zval ** obj, char *function_name,
			char *filename, int line_number TSRMLS_DC)
{
  if (fcic->initialized)
    return;

  zend_class_entry *obj_ce;
  obj_ce = Z_OBJCE_PP (obj);

  /*
   * we do not initialize fci.
   *   function_table  --  not initialized by zend_call_method
   *   function_name   --  zend_call_method initializes this to a pointer to
   *                       a zval 'z_fname', but does not initialize z_fname
   *                       in case of a method invocation
   *   retval_ptr_ptr  --  should be initialized by caller
   *   param_count     --  should be initialized by caller
   *   params          --  should be initialized by caller
   */
  fci->size = sizeof (*fci);
  fci->object_pp = obj;
  fci->no_separation = 1;
  fci->symbol_table = NULL;

  fcic->initialized = 1;
  fcic->calling_scope = obj_ce;
  fcic->object_pp = obj;
  fcic->function_handler
    = Z_OBJ_HT_PP (obj)->get_method (obj,
				     function_name,
				     strlen (function_name) TSRMLS_CC);

  if (fcic->function_handler == NULL)
    {
      phc_setup_error (1, filename, line_number, NULL TSRMLS_CC);
      php_error_docref (NULL TSRMLS_CC, E_ERROR,
			"Call to undefined method %s::%s",
			obj_ce->name, function_name);
    }
}

/*
 * Like initialize_method_call, but return 0 if no constructor is defined
 * rather than giving an error.
 */

static int 
initialize_constructor_call (zend_fcall_info * fci,
			     zend_fcall_info_cache * fcic, zval ** obj,
			     char *filename, int line_number TSRMLS_DC)
{
  if (fcic->initialized)
    return;

  zend_class_entry *obj_ce;
  obj_ce = Z_OBJCE_PP (obj);

  /*
   * we do not initialize fci.
   *   function_table  --  not initialized by zend_call_method
   *   function_name   --  zend_call_method initializes this to a pointer to
   *                       a zval 'z_fname', but does not initialize z_fname
   *                       in case of a method invocation
   *   retval_ptr_ptr  --  should be initialized by caller
   *   param_count     --  should be initialized by caller
   *   params          --  should be initialized by caller
   */
  fci->size = sizeof (*fci);
  fci->object_pp = obj;
  fci->no_separation = 1;
  fci->symbol_table = NULL;

  fcic->initialized = 1;
  fcic->calling_scope = obj_ce;
  fcic->object_pp = obj;
  fcic->function_handler
    = Z_OBJ_HT_PP (obj)->get_constructor (*obj TSRMLS_CC);

  return (fcic->function_handler != NULL);
}
// vi:set ts=8:

/*
 * Creates a copy of *in using persistent memory, optionally destroying *in
 *
 * Does not work for objects/resources and will loop on self-recursive arrays.
 */

zval *
persistent_clone (zval * in, int destroy_in TSRMLS_DC)
{
  zval *out = pemalloc (sizeof (zval), 1);
  *out = *in;

  switch (Z_TYPE_P (in))
    {
    case IS_NULL:
    case IS_LONG:
    case IS_DOUBLE:
    case IS_BOOL:
      /* nothing more to be done */
      break;
    case IS_STRING:
      Z_STRVAL_P (out) = pemalloc (Z_STRLEN_P (in) + 1, 1);
      memcpy (Z_STRVAL_P (out), Z_STRVAL_P (in), Z_STRLEN_P (in) + 1);
      break;
    case IS_ARRAY:
      {
	HashTable *old_arr = Z_ARRVAL_P (in);
	HashTable *new_arr = pemalloc (sizeof (HashTable), 1);
	zend_hash_init (new_arr, old_arr->nNumOfElements, NULL, ZVAL_PTR_DTOR,
			/* persistent */ 1);

	for (zend_hash_internal_pointer_reset (old_arr);
	     zend_hash_has_more_elements (old_arr) == SUCCESS;
	     zend_hash_move_forward (old_arr))
	  {
	    char *key;
	    uint keylen;
	    ulong idx;
	    int type;
	    zval **old_elem, *new_elem;

	    type =
	      zend_hash_get_current_key_ex (old_arr, &key, &keylen, &idx, 0,
					    NULL);
	    assert (zend_hash_get_current_data
		    (old_arr, (void **) &old_elem) == SUCCESS);

	    new_elem = persistent_clone (*old_elem, destroy_in TSRMLS_CC);

	    if (type == HASH_KEY_IS_STRING)
	      zend_hash_add (new_arr, key, keylen, &new_elem, sizeof (zval *),
			     NULL);
	    else
	      zend_hash_index_update (new_arr, idx, &new_elem,
				      sizeof (zval *), NULL);

	  }

	Z_ARRVAL_P (out) = new_arr;
      }
      break;
    default:
      /* other types are not supported */
      assert (0);
    }

  zval_ptr_dtor (&in);
  return out;
}

/*
 * Wrapper around zend_declare_property which 
 *
 * - Asserts that the ZEND_INTERNAL_CLASS flag is cleared
 *   (otherwise we cannot add complex (i.e., array) properties)
 * - Creates a persistent clone of the property to be added before
 *   calling zend_declare_property, since the memory for this property
 *   should only be deallocated when the module is shut down
 *   (and not when the request finishes)
 * - Cleans up after zend_declare_property by re-allocating the name of 
 *   the property using persistent memory, for much the same reason
 */

static int
phc_declare_property (zend_class_entry * ce, char *name, int name_length,
		      zval * property, int access_type TSRMLS_DC)
{
  assert (!(ce->type & ZEND_INTERNAL_CLASS));
  assert (zend_declare_property
	  (ce, name, name_length, persistent_clone (property, 1 TSRMLS_CC),
	   access_type TSRMLS_CC) == SUCCESS);

  zend_property_info *property_info;
  assert (zend_hash_find
	  (&ce->properties_info, name, name_length + 1,
	   (void **) &property_info) == SUCCESS);
  efree (property_info->name);
  property_info->name = name;

  return SUCCESS;
}

// vi:set ts=8:


static void
cast_var (zval ** p_zvp, int type)
{
  assert (type >= 0 && type <= 6);
  if ((*p_zvp)->type == type)
    return;

  sep_copy_on_write (p_zvp);
  zval *zvp = *p_zvp;

  switch (type)
    {
    case IS_NULL:
      convert_to_null (zvp);
      break;
    case IS_BOOL:
      convert_to_boolean (zvp);
      break;
    case IS_LONG:
      convert_to_long (zvp);
      break;
    case IS_DOUBLE:
      convert_to_double (zvp);
      break;
    case IS_STRING:
      convert_to_string (zvp);
      break;
    case IS_ARRAY:
      convert_to_array (zvp);
      break;
    case IS_OBJECT:
      convert_to_object (zvp);
      break;
    default:
      assert (0);		// TODO unimplemented
      break;
    }
}

/* Copies a constant into ZVP. Note that LENGTH does not include the NULL-terminating byte. */
static void
get_constant (char *name, int length, zval ** p_zvp TSRMLS_DC)
{
  MAKE_STD_ZVAL (*p_zvp);
  // zend_get_constant returns 1 for success, not SUCCESS
  int result = zend_get_constant (name, length, *p_zvp TSRMLS_CC);
  if (result == 0)
    ZVAL_STRINGL (*p_zvp, name, length, 1);
}

/* The function call mechanism deals specially with EG(uninitialize_zval_ptr)
 * (or sometime EG(uninitialize_zval)), so we need to use this too. This
 * particular zval can also be set, but there is an implicit guarantee 
 * of the information below.
 *
 * If assertions are off, this should be inlined to nothing.
 */
static void
phc_check_invariants (TSRMLS_D)
{
  assert (EG (uninitialized_zval_ptr) == &EG (uninitialized_zval));
  assert (EG (uninitialized_zval).refcount >= 1);
  assert (EG (uninitialized_zval).value.lval == 0);
  assert (EG (uninitialized_zval).type == IS_NULL);
  assert (EG (uninitialized_zval).is_ref == 0);
}


static int
check_unset_index_type (zval * ind TSRMLS_DC)
{
  if (Z_TYPE_P (ind) == IS_OBJECT || Z_TYPE_P (ind) == IS_ARRAY)
    {
      php_error_docref (NULL TSRMLS_CC, E_WARNING,
			"Illegal offset type in unset");
      return 0;
    }

  return 1;
}



/*
 * unset
 */

static void
unset_var (HashTable * st, char *name, int length)
{
  zend_hash_del (st, name, length);
}

static void
unset_array (zval ** p_var, zval * ind TSRMLS_DC)
{
  // NO error required
  if (Z_TYPE_PP (p_var) != IS_ARRAY)
    {
      if (Z_TYPE_PP (p_var) == IS_STRING)
	{
	  php_error_docref (NULL TSRMLS_CC, E_ERROR,
			    "Cannot unset string offsets");
	}
      else if (Z_TYPE_PP (p_var) != IS_NULL)
	{
	  php_error_docref (NULL TSRMLS_CC, E_WARNING,
			    "Cannot unset offsets in a non-array variable");
	}

      return;
    }

  // if its not an array, make it an array
  HashTable *ht = Z_ARRVAL_P (*p_var);

  ht_delete (ht, ind);
}

/*
 * Lookup variable whose name is var_var in st. We do not call
 * ht_find because ht_find uses zend_symtable_find to search for strings
 * rather than zend_hash_find. The difference is that zend_symtable_find
 * will convert strings to integers where possible: arrays are always
 * integer-indexed if at all possible. Variable names however should
 * _always_ be treated as strings.
 * 
 */

/*
 * If the parameter is a string, returns the parameter, with the refcount
 * incremented. If its not a string, returns a new zval, with a refcount of
 * 1. Either way, zval_dtor_ptr must be run by the caller on the return
 * value.
 */
zval*
get_string_val (zval* zvp)
{
  if (Z_TYPE_P (zvp) == IS_STRING)
    {
      zvp->refcount++;
      return zvp;
    }
  else
    {
      zval* clone = zvp_clone_ex (zvp);
      convert_to_string (clone);
      return clone;
    }
}

zval **
get_var_var (HashTable * st, zval * index TSRMLS_DC)
{
  zval* str_index = get_string_val (index);
  char* name = Z_STRVAL_P (str_index);
  int length = Z_STRLEN_P (str_index) + 1;
  unsigned long hash = zend_get_hash_value (name, length);

  zval** result = get_st_entry (st, name, length, hash TSRMLS_CC);
  zval_ptr_dtor (&str_index);
  return result;
}

/* 
 * Read the variable described by var_var from symbol table st
 * See comments for get_var_var
 */
zval *
read_var_var (HashTable * st, zval * index TSRMLS_DC)
{
  zval* str_index = get_string_val (index);
  char* name = Z_STRVAL_P (str_index);
  int length = Z_STRLEN_P (str_index) + 1;
  unsigned long hash = zend_get_hash_value (name, length);

  zval* result = read_var (st, name, length, hash TSRMLS_CC);
  zval_ptr_dtor (&str_index);
  return result;
}

static void
phc_builtin_eval (zval * arg, zval ** p_result, char *filename TSRMLS_DC)
{
  // If the user wrote "return ..", we need to store the
  // return value; however, in that case, zend_eval_string
  // will slap an extra "return" onto the front of the string,
  // so we must remove the "return" from the string the user
  // wrote. If the user did not write "return", he is not
  // interested in the return value, and we must pass NULL
  // instead or rhs to avoid zend_eval_string adding "return".

  // convert to a string
  // TODO avoid allocation
  zval *copy = zvp_clone_ex (arg);
  convert_to_string (copy);

  if (*p_result && !strncmp (Z_STRVAL_P (copy), "return ", 7))
    {
      zend_eval_string (Z_STRVAL_P (copy) + 7, *p_result,
			"eval'd code" TSRMLS_CC);
    }
  else
    {
      zend_eval_string (Z_STRVAL_P (copy), NULL, "eval'd code" TSRMLS_CC);
    }

  // cleanup
  assert (copy->refcount == 1);
  zval_ptr_dtor (&copy);
}

static void
phc_builtin_exit (zval * arg, zval ** p_result, char *filename TSRMLS_DC)
{
  if (Z_TYPE_P (arg) == IS_LONG)
    EG (exit_status) = Z_LVAL_P (arg);
  else
    zend_print_variable (arg);

  zend_bailout ();
}

static void
phc_builtin_die (zval * arg, zval ** p_result, char *filename TSRMLS_DC)
{
  phc_builtin_exit (arg, p_result, filename TSRMLS_CC);
}

static void
phc_builtin_echo (zval * arg, zval ** p_result TSRMLS_DC)
{
  assert (*p_result == NULL);
  zend_print_variable (arg);
}

static void
phc_builtin_print (zval * arg, zval ** p_result, char *filename TSRMLS_DC)
{
  zval *echo_arg = NULL;
  phc_builtin_echo (arg, &echo_arg TSRMLS_CC);

  if (*p_result)
    ZVAL_LONG (*p_result, 1);
}

// TODO is there a memory leak here is result has a value?
// TOOD isnt this just the same as isset
static void
phc_builtin_empty (zval * arg, zval ** p_result, char *filename TSRMLS_DC)
{
  assert (*p_result);
  ZVAL_BOOL (*p_result, !zend_is_true (arg));
}

// For require, include, require_once and include_once.

// Include:
//    return 1 for success
//    Warning, and return false for failure
// Require:
//    return 1 for success
//    Fail for failure
// Include_once
//    Return true if already included
//    Return 1 for success
//    Warning and return false for failure
// Require_once:
//    Return true if already included
//    return 1 for success
//    Fail for failure
//
static void
include_backend (zval * arg, zval ** p_result, char *filename, int type, int is_once, char* error, char* error_function TSRMLS_DC)
{
  // In the event that the Zend engine cannot find the file, after checking the
  // include path, it tries the current directory. It does this only if the
  // interpreter is executing, and it checks the interpreters opcodes for a
  // filename (see streams/plain_wrapper.c:1352)

  // An alternative is to add the directory to include_path, but its
  // semantically incorrect (get_included_path() would give the wrong answer),
  // and error prone (if people overwrite include_path).
  // TODO: though we could add it for this function only

  assert (EG (active_op_array) == NULL);
  assert (filename != NULL);

  zval *arg_file = arg;
  // Check we have a string
  if (Z_TYPE_P (arg_file) != IS_STRING)
    {
      arg_file = zvp_clone_ex (arg_file);
      convert_to_string (arg_file);
    }

  zend_file_handle handle;
  zend_op_array* new_op_array;
  zend_function zf;

  // Check the _ONCE varieties (based on zend_vm_def.h)
   if (is_once)
     {
       if (IS_ABSOLUTE_PATH (Z_STRVAL_P (arg_file), Z_STRLEN_P (arg_file)))
	 {
	   // Get the proper path name for require
	   cwd_state state;

	   state.cwd_length = 0;
	   state.cwd = malloc(1);
	   state.cwd[0] = 0;
	   int success = !virtual_file_ex(&state, Z_STRVAL_P(arg_file), NULL, 1)
	     && zend_hash_exists(&EG(included_files), state.cwd,
				 state.cwd_length+1);

	   free (state.cwd);

	   if (!success)
	     goto cleanup;
	 }
     }


   // Compile the file
   // Pretend the interpreter is running
   EG (in_execution) = 1;

   int success = zend_stream_open (Z_STRVAL_P (arg_file), &handle TSRMLS_CC);

   // Stop pretending
   EG (in_execution) = 0;
   EG (active_op_array) = NULL;

   if (success != SUCCESS)
     goto fail;


   if (is_once)
     {
       // Check it hadnt been included already
       int once_success = zend_hash_add_empty_element(&EG(included_files),
						      handle.opened_path,
						      strlen (handle.opened_path)+1);
       // Return true 
       if (once_success != SUCCESS)
	 {
	   ZVAL_BOOL (*p_result, 1);
	   goto cleanup;
	 }
     }

   if (!handle.opened_path)
     handle.opened_path = estrndup (Z_STRVAL_P(arg_file), Z_STRLEN_P (arg_file));

   // run it
   success = zend_execute_scripts (type TSRMLS_CC, p_result, 1, &handle);
   assert (success == SUCCESS);
   zend_stream_close (&handle);

   // Success
   if (*p_result)
       ZVAL_LONG (*p_result, 1);


   goto cleanup;


fail:

   php_error_docref (error_function
		     TSRMLS_CC, 
		     (type == ZEND_INCLUDE) ? E_WARNING : E_ERROR,
		     error,
		     php_strip_url_passwd (Z_STRVAL_P (arg_file)),
		     STR_PRINT (PG (include_path)));


   // Failure
   if (*p_result)
     ZVAL_BOOL (*p_result, 0);

cleanup:

   if (handle.opened_path)
     efree (handle.opened_path);
   zend_destroy_file_handle (&handle TSRMLS_CC);


  if (arg != arg_file)
    zval_ptr_dtor (&arg_file);
}

static void
phc_builtin_include (zval * arg, zval ** p_result, char *filename TSRMLS_DC)
{
  include_backend ( arg,
		    p_result,
		    filename,
		    ZEND_INCLUDE,
		    0,
		    "Failed opening '%s' for inclusion (include_path='%s')",
		    "function.include"
		    TSRMLS_CC);
}

static void
phc_builtin_require (zval * arg, zval ** p_result, char *filename TSRMLS_DC)
{
  include_backend ( arg,
		    p_result,
		    filename,
		    ZEND_REQUIRE,
		    0,
		    "Failed opening required '%s' (include_path='%s')",
		    "function.require"
		    TSRMLS_CC);
}

static void
phc_builtin_include_once (zval * arg, zval ** p_result, char *filename TSRMLS_DC)
{
  include_backend ( arg,
		    p_result,
		    filename,
		    ZEND_INCLUDE,
		    1,
		    "Failed opening '%s' for inclusion (include_path='%s')",
		    "function.include_once"
		    TSRMLS_CC);
}

static void
phc_builtin_require_once (zval * arg, zval ** p_result, char *filename TSRMLS_DC)
{
  include_backend ( arg,
		    p_result,
		    filename,
		    ZEND_REQUIRE,
		    1,
		    "Failed opening required '%s' (include_path='%s')",
		    "function.require_once"
		    TSRMLS_CC);
}

// END INCLUDED FILES
int saved_refcount;
static zend_fcall_info chr_fci;
static zend_fcall_info_cache chr_fcic = {0,NULL,NULL,NULL};
static zend_fcall_info ord_fci;
static zend_fcall_info_cache ord_fcic = {0,NULL,NULL,NULL};
static zend_fcall_info printf_fci;
static zend_fcall_info_cache printf_fcic = {0,NULL,NULL,NULL};
static zend_fcall_info strlen_fci;
static zend_fcall_info_cache strlen_fcic = {0,NULL,NULL,NULL};
static zend_fcall_info substr_fci;
static zend_fcall_info_cache substr_fcic = {0,NULL,NULL,NULL};
// class Endian
// {
// 	public static function convertEndianShort($short)
// 	{
// 		$TLE5 = 65535;
// 		$short = ($short & $TLE5);
// 		$TLE6 = 8;
// 		$firstBit = ($short >> $TLE6);
// 		$TLE7 = 8;
// 		$TLE8 = ($short >> $TLE7);
// 		$TLE9 = 8;
// 		$TLE10 = ($TLE8 << $TLE9);
// 		$short = ($short - $TLE10);
// 		$secondBit = $short;
// 		$TLE11 = 8;
// 		$TLE12 = ($secondBit << $TLE11);
// 		$TLE13 = ($TLE12 + $firstBit);
// 		return $TLE13;
// 	}
// 	public static function convertEndianInteger($int)
// 	{
// 		$TLE14 = 4294967295.0;
// 		$int = ($int & $TLE14);
// 		$TLE15 = 24;
// 		$TLE16 = ($int >> $TLE15);
// 		$TLE17 = 255;
// 		$firstBit = ($TLE16 & $TLE17);
// 		$TLE18 = 24;
// 		$TLE19 = ($int >> $TLE18);
// 		$TLE20 = 255;
// 		$TLE21 = ($TLE19 & $TLE20);
// 		$TLE22 = 24;
// 		$TLE23 = ($TLE21 << $TLE22);
// 		$int = ($int - $TLE23);
// 		$TLE24 = 16;
// 		$TLE25 = ($int >> $TLE24);
// 		$TLE26 = 255;
// 		$secondBit = ($TLE25 & $TLE26);
// 		$TLE27 = 16;
// 		$TLE28 = ($int >> $TLE27);
// 		$TLE29 = 255;
// 		$TLE30 = ($TLE28 & $TLE29);
// 		$TLE31 = 16;
// 		$TLE32 = ($TLE30 << $TLE31);
// 		$int = ($int - $TLE32);
// 		$TLE33 = 8;
// 		$TLE34 = ($int >> $TLE33);
// 		$TLE35 = 255;
// 		$tirthBit = ($TLE34 & $TLE35);
// 		$TLE36 = 8;
// 		$TLE37 = ($int >> $TLE36);
// 		$TLE38 = 255;
// 		$TLE39 = ($TLE37 & $TLE38);
// 		$TLE40 = 8;
// 		$TLE41 = ($TLE39 << $TLE40);
// 		$fourthBit = ($int - $TLE41);
// 		$TLE42 = 24;
// 		$TLE43 = ($fourthBit << $TLE42);
// 		$TLE44 = 16;
// 		$TLE45 = ($tirthBit << $TLE44);
// 		$TLE46 = ($TLE43 + $TLE45);
// 		$TLE47 = 8;
// 		$TLE48 = ($secondBit << $TLE47);
// 		$TLE49 = ($TLE46 + $TLE48);
// 		$TLE50 = ($TLE49 + $firstBit);
// 		return $TLE50;
// 	}
// }
// public static function convertEndianShort($short)
// {
// 	$TLE5 = 65535;
// 	$short = ($short & $TLE5);
// 	$TLE6 = 8;
// 	$firstBit = ($short >> $TLE6);
// 	$TLE7 = 8;
// 	$TLE8 = ($short >> $TLE7);
// 	$TLE9 = 8;
// 	$TLE10 = ($TLE8 << $TLE9);
// 	$short = ($short - $TLE10);
// 	$secondBit = $short;
// 	$TLE11 = 8;
// 	$TLE12 = ($secondBit << $TLE11);
// 	$TLE13 = ($TLE12 + $firstBit);
// 	return $TLE13;
// }
PHP_METHOD(Endian, convertEndianShort)
{
zval* local_TLE10 = NULL;
zval* local_TLE11 = NULL;
zval* local_TLE12 = NULL;
zval* local_TLE13 = NULL;
zval* local_TLE5 = NULL;
zval* local_TLE6 = NULL;
zval* local_TLE7 = NULL;
zval* local_TLE8 = NULL;
zval* local_TLE9 = NULL;
zval* local_firstBit = NULL;
zval* local_secondBit = NULL;
zval* local_short = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_short != NULL)
{
	zval_ptr_dtor (&local_short);
}
local_short = params[0];
}
// Function body
// $TLE5 = 65535;
{
        if (local_TLE5 == NULL)
    {
      local_TLE5 = EG (uninitialized_zval_ptr);
      local_TLE5->refcount++;
    }
  zval** p_lhs = &local_TLE5;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65535);

phc_check_invariants (TSRMLS_C);
}
// $short = ($short & $TLE5);
{
    if (local_short == NULL)
    {
      local_short = EG (uninitialized_zval_ptr);
      local_short->refcount++;
    }
  zval** p_lhs = &local_short;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE5 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE5;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE6 = 8;
{
        if (local_TLE6 == NULL)
    {
      local_TLE6 = EG (uninitialized_zval_ptr);
      local_TLE6->refcount++;
    }
  zval** p_lhs = &local_TLE6;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $firstBit = ($short >> $TLE6);
{
    if (local_firstBit == NULL)
    {
      local_firstBit = EG (uninitialized_zval_ptr);
      local_firstBit->refcount++;
    }
  zval** p_lhs = &local_firstBit;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE6 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE6;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE7 = 8;
{
        if (local_TLE7 == NULL)
    {
      local_TLE7 = EG (uninitialized_zval_ptr);
      local_TLE7->refcount++;
    }
  zval** p_lhs = &local_TLE7;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE8 = ($short >> $TLE7);
{
    if (local_TLE8 == NULL)
    {
      local_TLE8 = EG (uninitialized_zval_ptr);
      local_TLE8->refcount++;
    }
  zval** p_lhs = &local_TLE8;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE7 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE7;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE9 = 8;
{
        if (local_TLE9 == NULL)
    {
      local_TLE9 = EG (uninitialized_zval_ptr);
      local_TLE9->refcount++;
    }
  zval** p_lhs = &local_TLE9;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE10 = ($TLE8 << $TLE9);
{
    if (local_TLE10 == NULL)
    {
      local_TLE10 = EG (uninitialized_zval_ptr);
      local_TLE10->refcount++;
    }
  zval** p_lhs = &local_TLE10;

    zval* left;
  if (local_TLE8 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE8;

    zval* right;
  if (local_TLE9 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE9;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $short = ($short - $TLE10);
{
    if (local_short == NULL)
    {
      local_short = EG (uninitialized_zval_ptr);
      local_short->refcount++;
    }
  zval** p_lhs = &local_short;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE10 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE10;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $secondBit = $short;
{
    if (local_secondBit == NULL)
    {
      local_secondBit = EG (uninitialized_zval_ptr);
      local_secondBit->refcount++;
    }
  zval** p_lhs = &local_secondBit;

    zval* rhs;
  if (local_short == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_short;

  if (*p_lhs != rhs)
    {
        if ((*p_lhs)->is_ref)
      overwrite_lhs (*p_lhs, rhs);
  else
    {
      zval_ptr_dtor (p_lhs);
        if (rhs->is_ref)
    {
      // Take a copy of RHS for LHS
      *p_lhs = zvp_clone_ex (rhs);
    }
  else
    {
      // Share a copy
      rhs->refcount++;
      *p_lhs = rhs;
    }

    }

    }
phc_check_invariants (TSRMLS_C);
}
// $TLE11 = 8;
{
        if (local_TLE11 == NULL)
    {
      local_TLE11 = EG (uninitialized_zval_ptr);
      local_TLE11->refcount++;
    }
  zval** p_lhs = &local_TLE11;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE12 = ($secondBit << $TLE11);
{
    if (local_TLE12 == NULL)
    {
      local_TLE12 = EG (uninitialized_zval_ptr);
      local_TLE12->refcount++;
    }
  zval** p_lhs = &local_TLE12;

    zval* left;
  if (local_secondBit == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_secondBit;

    zval* right;
  if (local_TLE11 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE11;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE13 = ($TLE12 + $firstBit);
{
    if (local_TLE13 == NULL)
    {
      local_TLE13 = EG (uninitialized_zval_ptr);
      local_TLE13->refcount++;
    }
  zval** p_lhs = &local_TLE13;

    zval* left;
  if (local_TLE12 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE12;

    zval* right;
  if (local_firstBit == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_firstBit;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// return $TLE13;
{
     zval* rhs;
  if (local_TLE13 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE13;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE10 != NULL)
{
zval_ptr_dtor (&local_TLE10);
}
if (local_TLE11 != NULL)
{
zval_ptr_dtor (&local_TLE11);
}
if (local_TLE12 != NULL)
{
zval_ptr_dtor (&local_TLE12);
}
if (local_TLE13 != NULL)
{
zval_ptr_dtor (&local_TLE13);
}
if (local_TLE5 != NULL)
{
zval_ptr_dtor (&local_TLE5);
}
if (local_TLE6 != NULL)
{
zval_ptr_dtor (&local_TLE6);
}
if (local_TLE7 != NULL)
{
zval_ptr_dtor (&local_TLE7);
}
if (local_TLE8 != NULL)
{
zval_ptr_dtor (&local_TLE8);
}
if (local_TLE9 != NULL)
{
zval_ptr_dtor (&local_TLE9);
}
if (local_firstBit != NULL)
{
zval_ptr_dtor (&local_firstBit);
}
if (local_secondBit != NULL)
{
zval_ptr_dtor (&local_secondBit);
}
if (local_short != NULL)
{
zval_ptr_dtor (&local_short);
}
}
// public static function convertEndianInteger($int)
// {
// 	$TLE14 = 4294967295.0;
// 	$int = ($int & $TLE14);
// 	$TLE15 = 24;
// 	$TLE16 = ($int >> $TLE15);
// 	$TLE17 = 255;
// 	$firstBit = ($TLE16 & $TLE17);
// 	$TLE18 = 24;
// 	$TLE19 = ($int >> $TLE18);
// 	$TLE20 = 255;
// 	$TLE21 = ($TLE19 & $TLE20);
// 	$TLE22 = 24;
// 	$TLE23 = ($TLE21 << $TLE22);
// 	$int = ($int - $TLE23);
// 	$TLE24 = 16;
// 	$TLE25 = ($int >> $TLE24);
// 	$TLE26 = 255;
// 	$secondBit = ($TLE25 & $TLE26);
// 	$TLE27 = 16;
// 	$TLE28 = ($int >> $TLE27);
// 	$TLE29 = 255;
// 	$TLE30 = ($TLE28 & $TLE29);
// 	$TLE31 = 16;
// 	$TLE32 = ($TLE30 << $TLE31);
// 	$int = ($int - $TLE32);
// 	$TLE33 = 8;
// 	$TLE34 = ($int >> $TLE33);
// 	$TLE35 = 255;
// 	$tirthBit = ($TLE34 & $TLE35);
// 	$TLE36 = 8;
// 	$TLE37 = ($int >> $TLE36);
// 	$TLE38 = 255;
// 	$TLE39 = ($TLE37 & $TLE38);
// 	$TLE40 = 8;
// 	$TLE41 = ($TLE39 << $TLE40);
// 	$fourthBit = ($int - $TLE41);
// 	$TLE42 = 24;
// 	$TLE43 = ($fourthBit << $TLE42);
// 	$TLE44 = 16;
// 	$TLE45 = ($tirthBit << $TLE44);
// 	$TLE46 = ($TLE43 + $TLE45);
// 	$TLE47 = 8;
// 	$TLE48 = ($secondBit << $TLE47);
// 	$TLE49 = ($TLE46 + $TLE48);
// 	$TLE50 = ($TLE49 + $firstBit);
// 	return $TLE50;
// }
PHP_METHOD(Endian, convertEndianInteger)
{
zval* local_TLE14 = NULL;
zval* local_TLE15 = NULL;
zval* local_TLE16 = NULL;
zval* local_TLE17 = NULL;
zval* local_TLE18 = NULL;
zval* local_TLE19 = NULL;
zval* local_TLE20 = NULL;
zval* local_TLE21 = NULL;
zval* local_TLE22 = NULL;
zval* local_TLE23 = NULL;
zval* local_TLE24 = NULL;
zval* local_TLE25 = NULL;
zval* local_TLE26 = NULL;
zval* local_TLE27 = NULL;
zval* local_TLE28 = NULL;
zval* local_TLE29 = NULL;
zval* local_TLE30 = NULL;
zval* local_TLE31 = NULL;
zval* local_TLE32 = NULL;
zval* local_TLE33 = NULL;
zval* local_TLE34 = NULL;
zval* local_TLE35 = NULL;
zval* local_TLE36 = NULL;
zval* local_TLE37 = NULL;
zval* local_TLE38 = NULL;
zval* local_TLE39 = NULL;
zval* local_TLE40 = NULL;
zval* local_TLE41 = NULL;
zval* local_TLE42 = NULL;
zval* local_TLE43 = NULL;
zval* local_TLE44 = NULL;
zval* local_TLE45 = NULL;
zval* local_TLE46 = NULL;
zval* local_TLE47 = NULL;
zval* local_TLE48 = NULL;
zval* local_TLE49 = NULL;
zval* local_TLE50 = NULL;
zval* local_firstBit = NULL;
zval* local_fourthBit = NULL;
zval* local_int = NULL;
zval* local_secondBit = NULL;
zval* local_tirthBit = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_int != NULL)
{
	zval_ptr_dtor (&local_int);
}
local_int = params[0];
}
// Function body
// $TLE14 = 4294967295.0;
{
        if (local_TLE14 == NULL)
    {
      local_TLE14 = EG (uninitialized_zval_ptr);
      local_TLE14->refcount++;
    }
  zval** p_lhs = &local_TLE14;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   {
	unsigned char val[] = {
0, 0, 224, 255, 255, 255, 239, 65, };
ZVAL_DOUBLE (value, *(double*)(val));
}

phc_check_invariants (TSRMLS_C);
}
// $int = ($int & $TLE14);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE14 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE14;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE15 = 24;
{
        if (local_TLE15 == NULL)
    {
      local_TLE15 = EG (uninitialized_zval_ptr);
      local_TLE15->refcount++;
    }
  zval** p_lhs = &local_TLE15;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 24);

phc_check_invariants (TSRMLS_C);
}
// $TLE16 = ($int >> $TLE15);
{
    if (local_TLE16 == NULL)
    {
      local_TLE16 = EG (uninitialized_zval_ptr);
      local_TLE16->refcount++;
    }
  zval** p_lhs = &local_TLE16;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE15 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE15;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE17 = 255;
{
        if (local_TLE17 == NULL)
    {
      local_TLE17 = EG (uninitialized_zval_ptr);
      local_TLE17->refcount++;
    }
  zval** p_lhs = &local_TLE17;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $firstBit = ($TLE16 & $TLE17);
{
    if (local_firstBit == NULL)
    {
      local_firstBit = EG (uninitialized_zval_ptr);
      local_firstBit->refcount++;
    }
  zval** p_lhs = &local_firstBit;

    zval* left;
  if (local_TLE16 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE16;

    zval* right;
  if (local_TLE17 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE17;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE18 = 24;
{
        if (local_TLE18 == NULL)
    {
      local_TLE18 = EG (uninitialized_zval_ptr);
      local_TLE18->refcount++;
    }
  zval** p_lhs = &local_TLE18;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 24);

phc_check_invariants (TSRMLS_C);
}
// $TLE19 = ($int >> $TLE18);
{
    if (local_TLE19 == NULL)
    {
      local_TLE19 = EG (uninitialized_zval_ptr);
      local_TLE19->refcount++;
    }
  zval** p_lhs = &local_TLE19;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE18 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE18;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE20 = 255;
{
        if (local_TLE20 == NULL)
    {
      local_TLE20 = EG (uninitialized_zval_ptr);
      local_TLE20->refcount++;
    }
  zval** p_lhs = &local_TLE20;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE21 = ($TLE19 & $TLE20);
{
    if (local_TLE21 == NULL)
    {
      local_TLE21 = EG (uninitialized_zval_ptr);
      local_TLE21->refcount++;
    }
  zval** p_lhs = &local_TLE21;

    zval* left;
  if (local_TLE19 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE19;

    zval* right;
  if (local_TLE20 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE20;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE22 = 24;
{
        if (local_TLE22 == NULL)
    {
      local_TLE22 = EG (uninitialized_zval_ptr);
      local_TLE22->refcount++;
    }
  zval** p_lhs = &local_TLE22;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 24);

phc_check_invariants (TSRMLS_C);
}
// $TLE23 = ($TLE21 << $TLE22);
{
    if (local_TLE23 == NULL)
    {
      local_TLE23 = EG (uninitialized_zval_ptr);
      local_TLE23->refcount++;
    }
  zval** p_lhs = &local_TLE23;

    zval* left;
  if (local_TLE21 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE21;

    zval* right;
  if (local_TLE22 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE22;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $int = ($int - $TLE23);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE23 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE23;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE24 = 16;
{
        if (local_TLE24 == NULL)
    {
      local_TLE24 = EG (uninitialized_zval_ptr);
      local_TLE24->refcount++;
    }
  zval** p_lhs = &local_TLE24;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 16);

phc_check_invariants (TSRMLS_C);
}
// $TLE25 = ($int >> $TLE24);
{
    if (local_TLE25 == NULL)
    {
      local_TLE25 = EG (uninitialized_zval_ptr);
      local_TLE25->refcount++;
    }
  zval** p_lhs = &local_TLE25;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE24 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE24;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE26 = 255;
{
        if (local_TLE26 == NULL)
    {
      local_TLE26 = EG (uninitialized_zval_ptr);
      local_TLE26->refcount++;
    }
  zval** p_lhs = &local_TLE26;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $secondBit = ($TLE25 & $TLE26);
{
    if (local_secondBit == NULL)
    {
      local_secondBit = EG (uninitialized_zval_ptr);
      local_secondBit->refcount++;
    }
  zval** p_lhs = &local_secondBit;

    zval* left;
  if (local_TLE25 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE25;

    zval* right;
  if (local_TLE26 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE26;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE27 = 16;
{
        if (local_TLE27 == NULL)
    {
      local_TLE27 = EG (uninitialized_zval_ptr);
      local_TLE27->refcount++;
    }
  zval** p_lhs = &local_TLE27;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 16);

phc_check_invariants (TSRMLS_C);
}
// $TLE28 = ($int >> $TLE27);
{
    if (local_TLE28 == NULL)
    {
      local_TLE28 = EG (uninitialized_zval_ptr);
      local_TLE28->refcount++;
    }
  zval** p_lhs = &local_TLE28;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE27 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE27;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE29 = 255;
{
        if (local_TLE29 == NULL)
    {
      local_TLE29 = EG (uninitialized_zval_ptr);
      local_TLE29->refcount++;
    }
  zval** p_lhs = &local_TLE29;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE30 = ($TLE28 & $TLE29);
{
    if (local_TLE30 == NULL)
    {
      local_TLE30 = EG (uninitialized_zval_ptr);
      local_TLE30->refcount++;
    }
  zval** p_lhs = &local_TLE30;

    zval* left;
  if (local_TLE28 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE28;

    zval* right;
  if (local_TLE29 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE29;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE31 = 16;
{
        if (local_TLE31 == NULL)
    {
      local_TLE31 = EG (uninitialized_zval_ptr);
      local_TLE31->refcount++;
    }
  zval** p_lhs = &local_TLE31;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 16);

phc_check_invariants (TSRMLS_C);
}
// $TLE32 = ($TLE30 << $TLE31);
{
    if (local_TLE32 == NULL)
    {
      local_TLE32 = EG (uninitialized_zval_ptr);
      local_TLE32->refcount++;
    }
  zval** p_lhs = &local_TLE32;

    zval* left;
  if (local_TLE30 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE30;

    zval* right;
  if (local_TLE31 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE31;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $int = ($int - $TLE32);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE32 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE32;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE33 = 8;
{
        if (local_TLE33 == NULL)
    {
      local_TLE33 = EG (uninitialized_zval_ptr);
      local_TLE33->refcount++;
    }
  zval** p_lhs = &local_TLE33;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE34 = ($int >> $TLE33);
{
    if (local_TLE34 == NULL)
    {
      local_TLE34 = EG (uninitialized_zval_ptr);
      local_TLE34->refcount++;
    }
  zval** p_lhs = &local_TLE34;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE33 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE33;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE35 = 255;
{
        if (local_TLE35 == NULL)
    {
      local_TLE35 = EG (uninitialized_zval_ptr);
      local_TLE35->refcount++;
    }
  zval** p_lhs = &local_TLE35;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $tirthBit = ($TLE34 & $TLE35);
{
    if (local_tirthBit == NULL)
    {
      local_tirthBit = EG (uninitialized_zval_ptr);
      local_tirthBit->refcount++;
    }
  zval** p_lhs = &local_tirthBit;

    zval* left;
  if (local_TLE34 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE34;

    zval* right;
  if (local_TLE35 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE35;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE36 = 8;
{
        if (local_TLE36 == NULL)
    {
      local_TLE36 = EG (uninitialized_zval_ptr);
      local_TLE36->refcount++;
    }
  zval** p_lhs = &local_TLE36;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE37 = ($int >> $TLE36);
{
    if (local_TLE37 == NULL)
    {
      local_TLE37 = EG (uninitialized_zval_ptr);
      local_TLE37->refcount++;
    }
  zval** p_lhs = &local_TLE37;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE36 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE36;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE38 = 255;
{
        if (local_TLE38 == NULL)
    {
      local_TLE38 = EG (uninitialized_zval_ptr);
      local_TLE38->refcount++;
    }
  zval** p_lhs = &local_TLE38;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE39 = ($TLE37 & $TLE38);
{
    if (local_TLE39 == NULL)
    {
      local_TLE39 = EG (uninitialized_zval_ptr);
      local_TLE39->refcount++;
    }
  zval** p_lhs = &local_TLE39;

    zval* left;
  if (local_TLE37 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE37;

    zval* right;
  if (local_TLE38 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE38;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE40 = 8;
{
        if (local_TLE40 == NULL)
    {
      local_TLE40 = EG (uninitialized_zval_ptr);
      local_TLE40->refcount++;
    }
  zval** p_lhs = &local_TLE40;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE41 = ($TLE39 << $TLE40);
{
    if (local_TLE41 == NULL)
    {
      local_TLE41 = EG (uninitialized_zval_ptr);
      local_TLE41->refcount++;
    }
  zval** p_lhs = &local_TLE41;

    zval* left;
  if (local_TLE39 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE39;

    zval* right;
  if (local_TLE40 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE40;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $fourthBit = ($int - $TLE41);
{
    if (local_fourthBit == NULL)
    {
      local_fourthBit = EG (uninitialized_zval_ptr);
      local_fourthBit->refcount++;
    }
  zval** p_lhs = &local_fourthBit;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE41 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE41;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE42 = 24;
{
        if (local_TLE42 == NULL)
    {
      local_TLE42 = EG (uninitialized_zval_ptr);
      local_TLE42->refcount++;
    }
  zval** p_lhs = &local_TLE42;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 24);

phc_check_invariants (TSRMLS_C);
}
// $TLE43 = ($fourthBit << $TLE42);
{
    if (local_TLE43 == NULL)
    {
      local_TLE43 = EG (uninitialized_zval_ptr);
      local_TLE43->refcount++;
    }
  zval** p_lhs = &local_TLE43;

    zval* left;
  if (local_fourthBit == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_fourthBit;

    zval* right;
  if (local_TLE42 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE42;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE44 = 16;
{
        if (local_TLE44 == NULL)
    {
      local_TLE44 = EG (uninitialized_zval_ptr);
      local_TLE44->refcount++;
    }
  zval** p_lhs = &local_TLE44;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 16);

phc_check_invariants (TSRMLS_C);
}
// $TLE45 = ($tirthBit << $TLE44);
{
    if (local_TLE45 == NULL)
    {
      local_TLE45 = EG (uninitialized_zval_ptr);
      local_TLE45->refcount++;
    }
  zval** p_lhs = &local_TLE45;

    zval* left;
  if (local_tirthBit == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_tirthBit;

    zval* right;
  if (local_TLE44 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE44;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE46 = ($TLE43 + $TLE45);
{
    if (local_TLE46 == NULL)
    {
      local_TLE46 = EG (uninitialized_zval_ptr);
      local_TLE46->refcount++;
    }
  zval** p_lhs = &local_TLE46;

    zval* left;
  if (local_TLE43 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE43;

    zval* right;
  if (local_TLE45 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE45;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE47 = 8;
{
        if (local_TLE47 == NULL)
    {
      local_TLE47 = EG (uninitialized_zval_ptr);
      local_TLE47->refcount++;
    }
  zval** p_lhs = &local_TLE47;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE48 = ($secondBit << $TLE47);
{
    if (local_TLE48 == NULL)
    {
      local_TLE48 = EG (uninitialized_zval_ptr);
      local_TLE48->refcount++;
    }
  zval** p_lhs = &local_TLE48;

    zval* left;
  if (local_secondBit == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_secondBit;

    zval* right;
  if (local_TLE47 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE47;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE49 = ($TLE46 + $TLE48);
{
    if (local_TLE49 == NULL)
    {
      local_TLE49 = EG (uninitialized_zval_ptr);
      local_TLE49->refcount++;
    }
  zval** p_lhs = &local_TLE49;

    zval* left;
  if (local_TLE46 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE46;

    zval* right;
  if (local_TLE48 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE48;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE50 = ($TLE49 + $firstBit);
{
    if (local_TLE50 == NULL)
    {
      local_TLE50 = EG (uninitialized_zval_ptr);
      local_TLE50->refcount++;
    }
  zval** p_lhs = &local_TLE50;

    zval* left;
  if (local_TLE49 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE49;

    zval* right;
  if (local_firstBit == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_firstBit;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// return $TLE50;
{
     zval* rhs;
  if (local_TLE50 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE50;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE14 != NULL)
{
zval_ptr_dtor (&local_TLE14);
}
if (local_TLE15 != NULL)
{
zval_ptr_dtor (&local_TLE15);
}
if (local_TLE16 != NULL)
{
zval_ptr_dtor (&local_TLE16);
}
if (local_TLE17 != NULL)
{
zval_ptr_dtor (&local_TLE17);
}
if (local_TLE18 != NULL)
{
zval_ptr_dtor (&local_TLE18);
}
if (local_TLE19 != NULL)
{
zval_ptr_dtor (&local_TLE19);
}
if (local_TLE20 != NULL)
{
zval_ptr_dtor (&local_TLE20);
}
if (local_TLE21 != NULL)
{
zval_ptr_dtor (&local_TLE21);
}
if (local_TLE22 != NULL)
{
zval_ptr_dtor (&local_TLE22);
}
if (local_TLE23 != NULL)
{
zval_ptr_dtor (&local_TLE23);
}
if (local_TLE24 != NULL)
{
zval_ptr_dtor (&local_TLE24);
}
if (local_TLE25 != NULL)
{
zval_ptr_dtor (&local_TLE25);
}
if (local_TLE26 != NULL)
{
zval_ptr_dtor (&local_TLE26);
}
if (local_TLE27 != NULL)
{
zval_ptr_dtor (&local_TLE27);
}
if (local_TLE28 != NULL)
{
zval_ptr_dtor (&local_TLE28);
}
if (local_TLE29 != NULL)
{
zval_ptr_dtor (&local_TLE29);
}
if (local_TLE30 != NULL)
{
zval_ptr_dtor (&local_TLE30);
}
if (local_TLE31 != NULL)
{
zval_ptr_dtor (&local_TLE31);
}
if (local_TLE32 != NULL)
{
zval_ptr_dtor (&local_TLE32);
}
if (local_TLE33 != NULL)
{
zval_ptr_dtor (&local_TLE33);
}
if (local_TLE34 != NULL)
{
zval_ptr_dtor (&local_TLE34);
}
if (local_TLE35 != NULL)
{
zval_ptr_dtor (&local_TLE35);
}
if (local_TLE36 != NULL)
{
zval_ptr_dtor (&local_TLE36);
}
if (local_TLE37 != NULL)
{
zval_ptr_dtor (&local_TLE37);
}
if (local_TLE38 != NULL)
{
zval_ptr_dtor (&local_TLE38);
}
if (local_TLE39 != NULL)
{
zval_ptr_dtor (&local_TLE39);
}
if (local_TLE40 != NULL)
{
zval_ptr_dtor (&local_TLE40);
}
if (local_TLE41 != NULL)
{
zval_ptr_dtor (&local_TLE41);
}
if (local_TLE42 != NULL)
{
zval_ptr_dtor (&local_TLE42);
}
if (local_TLE43 != NULL)
{
zval_ptr_dtor (&local_TLE43);
}
if (local_TLE44 != NULL)
{
zval_ptr_dtor (&local_TLE44);
}
if (local_TLE45 != NULL)
{
zval_ptr_dtor (&local_TLE45);
}
if (local_TLE46 != NULL)
{
zval_ptr_dtor (&local_TLE46);
}
if (local_TLE47 != NULL)
{
zval_ptr_dtor (&local_TLE47);
}
if (local_TLE48 != NULL)
{
zval_ptr_dtor (&local_TLE48);
}
if (local_TLE49 != NULL)
{
zval_ptr_dtor (&local_TLE49);
}
if (local_TLE50 != NULL)
{
zval_ptr_dtor (&local_TLE50);
}
if (local_firstBit != NULL)
{
zval_ptr_dtor (&local_firstBit);
}
if (local_fourthBit != NULL)
{
zval_ptr_dtor (&local_fourthBit);
}
if (local_int != NULL)
{
zval_ptr_dtor (&local_int);
}
if (local_secondBit != NULL)
{
zval_ptr_dtor (&local_secondBit);
}
if (local_tirthBit != NULL)
{
zval_ptr_dtor (&local_tirthBit);
}
}
// ArgInfo structures (necessary to support compile time pass-by-reference)
ZEND_BEGIN_ARG_INFO_EX(Endian_convertEndianShort_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "short")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Endian_convertEndianInteger_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "int")
ZEND_END_ARG_INFO()

static function_entry Endian_functions[] = {
PHP_ME(Endian, convertEndianShort, Endian_convertEndianShort_arg_info, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
PHP_ME(Endian, convertEndianInteger, Endian_convertEndianInteger_arg_info, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
{ NULL, NULL, NULL }
};
// class Memory
// {
// 	public $_mem = NULL;
// 	public $_buffer = NULL;
// 	public $_pos = NULL;
// 	public $_readPos = NULL;
// 	public function __construct($memorySize = 0)
// 	{
// 		$this->setMemorySize($memorySize);
// 	}
// 	public function addByte($byte)
// 	{
// 		$TSt51 =& $this->_buffer;
// 		$TLE52 = 255;
// 		$TLE53 = ($byte & $TLE52);
// 		$TLE54 = chr($TLE53);
// 		$TSt51 = ($TSt51 . $TLE54);
// 		$TSt55 = $this->_pos;
// 		$TSt56 =& $this->_mem;
// 		$TLE57 = 255;
// 		$TLE58 = ($byte & $TLE57);
// 		$TSt56[$TSt55] = $TLE58;
// 		$TSt59 =& $this->_pos;
// 		++$TSt59;
// 	}
// 	public function addString($string)
// 	{
// 		$i = 0;
// 		$ElcfPF0 = True;
// 	L333:
// 		if (ElcfPF0) goto L323 else goto L324;
// 	L323:
// 		$ElcfPF0 = False;
// 		goto L325;
// 	L324:
// 		++$i;
// 		goto L325;
// 	L325:
// 		$TLE60 = strlen($string);
// 		$TLE61 = ($i < $TLE60);
// 		if (TLE61) goto L327 else goto L328;
// 	L327:
// 		goto L329;
// 	L328:
// 		goto L326;
// 		goto L329;
// 	L329:
// 		$TLE315 = param_is_ref (NULL, "ord", 0);
// 		if (TLE315) goto L330 else goto L331;
// 	L330:
// 		$TMIi314 =& $string[$i];
// 		goto L332;
// 	L331:
// 		$TMIi314 = $string[$i];
// 		goto L332;
// 	L332:
// 		$TLE62 = ord($TMIi314);
// 		$this->addByte($TLE62);
// 		goto L333;
// 	L326:
// 	}
// 	public function addShort($short)
// 	{
// 		$TLE63 = 65535;
// 		$short = ($short & $TLE63);
// 		$TLE64 = 8;
// 		$TLE65 = ($short >> $TLE64);
// 		$this->addByte($TLE65);
// 		$TLE66 = 8;
// 		$TLE67 = ($short >> $TLE66);
// 		$TLE68 = 8;
// 		$TLE69 = ($TLE67 << $TLE68);
// 		$short = ($short - $TLE69);
// 		$this->addByte($short);
// 	}
// 	public function addInteger($int)
// 	{
// 		$TLE70 = 4294967295.0;
// 		$int = ($int & $TLE70);
// 		$TLE71 = 24;
// 		$TLE72 = ($int >> $TLE71);
// 		$TLE73 = 255;
// 		$TLE74 = ($TLE72 & $TLE73);
// 		$this->addByte($TLE74);
// 		$TLE75 = 24;
// 		$TLE76 = ($int >> $TLE75);
// 		$TLE77 = 255;
// 		$TLE78 = ($TLE76 & $TLE77);
// 		$TLE79 = 24;
// 		$TLE80 = ($TLE78 << $TLE79);
// 		$int = ($int - $TLE80);
// 		$TLE81 = 16;
// 		$TLE82 = ($int >> $TLE81);
// 		$TLE83 = 255;
// 		$TLE84 = ($TLE82 & $TLE83);
// 		$this->addByte($TLE84);
// 		$TLE85 = 16;
// 		$TLE86 = ($int >> $TLE85);
// 		$TLE87 = 255;
// 		$TLE88 = ($TLE86 & $TLE87);
// 		$TLE89 = 16;
// 		$TLE90 = ($TLE88 << $TLE89);
// 		$int = ($int - $TLE90);
// 		$TLE91 = 8;
// 		$TLE92 = ($int >> $TLE91);
// 		$TLE93 = 255;
// 		$TLE94 = ($TLE92 & $TLE93);
// 		$this->addByte($TLE94);
// 		$TLE95 = 8;
// 		$TLE96 = ($int >> $TLE95);
// 		$TLE97 = 255;
// 		$TLE98 = ($TLE96 & $TLE97);
// 		$TLE99 = 8;
// 		$TLE100 = ($TLE98 << $TLE99);
// 		$int = ($int - $TLE100);
// 		$this->addByte($int);
// 	}
// 	public function readByte()
// 	{
// 		$TSt101 =& $this->_readPos;
// 		$TSt102 = $this->_mem;
// 		$TSi103 = $TSt102[$TSt101];
// 		++$TSt101;
// 		return $TSi103;
// 	}
// 	public function readShort()
// 	{
// 		$TLE104 = $this->readByte();
// 		$TLE105 = 8;
// 		$short = ($TLE104 << $TLE105);
// 		$TLE106 = $this->readByte();
// 		$short = ($short + $TLE106);
// 		return $short;
// 	}
// 	public function readInteger()
// 	{
// 		$TLE107 = $this->readByte();
// 		$TLE108 = 24;
// 		$int = ($TLE107 << $TLE108);
// 		$TLE109 = $this->readByte();
// 		$TLE110 = 16;
// 		$TLE111 = ($TLE109 << $TLE110);
// 		$int = ($int + $TLE111);
// 		$TLE112 = $this->readByte();
// 		$TLE113 = 8;
// 		$TLE114 = ($TLE112 << $TLE113);
// 		$int = ($int + $TLE114);
// 		$TLE115 = $this->readByte();
// 		$int = ($int + $TLE115);
// 		$TLE116 = (int) $int;
// 		return $TLE116;
// 	}
// 	public function resetReadPointer()
// 	{
// 		$TLE117 = 0;
// 		$this->_readPos = $TLE117;
// 	}
// 	public function setReadPointer($value)
// 	{
// 		$this->_readPos = $value;
// 	}
// 	public function setByte($pos, $byte)
// 	{
// 		$TSt118 =& $this->_buffer;
// 		$TLE119 = 255;
// 		$TLE120 = ($byte & $TLE119);
// 		$TLE121 = chr($TLE120);
// 		$TSt118[$pos] = $TLE121;
// 		$TSt122 =& $this->_mem;
// 		$TLE123 = 255;
// 		$TLE124 = ($byte & $TLE123);
// 		$TSt122[$pos] = $TLE124;
// 	}
// 	public function setShort($pos, $short)
// 	{
// 		$TLE125 = 65535;
// 		$short = ($short & $TLE125);
// 		$TLE126 = 8;
// 		$TLE127 = ($short >> $TLE126);
// 		$this->setByte($pos, $TLE127);
// 		$TLE128 = 8;
// 		$TLE129 = ($short >> $TLE128);
// 		$TLE130 = 8;
// 		$TLE131 = ($TLE129 << $TLE130);
// 		$short = ($short - $TLE131);
// 		$TLE132 = 1;
// 		$TLE133 = ($pos + $TLE132);
// 		$this->setByte($TLE133, $short);
// 	}
// 	public function setInteger($pos, $int)
// 	{
// 		$TLE134 = 4294967295.0;
// 		$int = ($int & $TLE134);
// 		$TLE135 = 24;
// 		$TLE136 = ($int >> $TLE135);
// 		$TLE137 = 255;
// 		$TLE138 = ($TLE136 & $TLE137);
// 		$this->setByte($pos, $TLE138);
// 		$TLE139 = 24;
// 		$TLE140 = ($int >> $TLE139);
// 		$TLE141 = 255;
// 		$TLE142 = ($TLE140 & $TLE141);
// 		$TLE143 = 24;
// 		$TLE144 = ($TLE142 << $TLE143);
// 		$int = ($int - $TLE144);
// 		$TLE145 = 1;
// 		$TLE146 = ($pos + $TLE145);
// 		$TLE147 = 16;
// 		$TLE148 = ($int >> $TLE147);
// 		$TLE149 = 255;
// 		$TLE150 = ($TLE148 & $TLE149);
// 		$this->setByte($TLE146, $TLE150);
// 		$TLE151 = 16;
// 		$TLE152 = ($int >> $TLE151);
// 		$TLE153 = 255;
// 		$TLE154 = ($TLE152 & $TLE153);
// 		$TLE155 = 16;
// 		$TLE156 = ($TLE154 << $TLE155);
// 		$int = ($int - $TLE156);
// 		$TLE157 = 2;
// 		$TLE158 = ($pos + $TLE157);
// 		$TLE159 = 8;
// 		$TLE160 = ($int >> $TLE159);
// 		$TLE161 = 255;
// 		$TLE162 = ($TLE160 & $TLE161);
// 		$this->setByte($TLE158, $TLE162);
// 		$TLE163 = 8;
// 		$TLE164 = ($int >> $TLE163);
// 		$TLE165 = 255;
// 		$TLE166 = ($TLE164 & $TLE165);
// 		$TLE167 = 8;
// 		$TLE168 = ($TLE166 << $TLE167);
// 		$int = ($int - $TLE168);
// 		$TLE169 = 3;
// 		$TLE170 = ($pos + $TLE169);
// 		$this->setByte($TLE170, $int);
// 	}
// 	public function getByte($pos)
// 	{
// 		$TSt171 = $this->_mem;
// 		$TSi172 = $TSt171[$pos];
// 		return $TSi172;
// 	}
// 	public function getShort($pos)
// 	{
// 		$TLE173 = $this->getByte($pos);
// 		$TLE174 = 8;
// 		$short = ($TLE173 << $TLE174);
// 		$TLE175 = 1;
// 		$TLE176 = ($pos + $TLE175);
// 		$TLE177 = $this->getByte($TLE176);
// 		$short = ($short + $TLE177);
// 		return $short;
// 	}
// 	public function getInteger($pos)
// 	{
// 		$TLE178 = $this->getByte($pos);
// 		$TLE179 = 24;
// 		$int = ($TLE178 << $TLE179);
// 		$TLE180 = 1;
// 		$TLE181 = ($pos + $TLE180);
// 		$TLE182 = $this->getByte($TLE181);
// 		$TLE183 = 16;
// 		$TLE184 = ($TLE182 << $TLE183);
// 		$int = ($int + $TLE184);
// 		$TLE185 = 2;
// 		$TLE186 = ($pos + $TLE185);
// 		$TLE187 = $this->getByte($TLE186);
// 		$TLE188 = 8;
// 		$TLE189 = ($TLE187 << $TLE188);
// 		$int = ($int + $TLE189);
// 		$TLE190 = 3;
// 		$TLE191 = ($pos + $TLE190);
// 		$TLE192 = $this->getByte($TLE191);
// 		$int = ($int + $TLE192);
// 		$TLE193 = (int) $int;
// 		return $TLE193;
// 	}
// 	public function getMemory($startPos = 0, $endPos = -1)
// 	{
// 		$TLE194 = 0;
// 		$TLE3 = ($startPos == $TLE194);
// 		if (TLE3) goto L334 else goto L335;
// 	L334:
// 		$TLE195 = -1;
// 		$TEF4 = ($endPos == $TLE195);
// 		goto L336;
// 	L335:
// 		$TEF4 = $TLE3;
// 		goto L336;
// 	L336:
// 		$TLE196 = (bool) $TEF4;
// 		if (TLE196) goto L343 else goto L344;
// 	L343:
// 		$TSt197 = $this->_buffer;
// 		return $TSt197;
// 		goto L345;
// 	L344:
// 		$TLE198 = -1;
// 		$TLE199 = ($endPos == $TLE198);
// 		if (TLE199) goto L337 else goto L338;
// 	L337:
// 		$TSt200 = $this->_pos;
// 		$endPos = $TSt200;
// 		goto L339;
// 	L338:
// 		goto L339;
// 	L339:
// 		$TLE317 = param_is_ref (NULL, "substr", 0);
// 		if (TLE317) goto L340 else goto L341;
// 	L340:
// 		$TMIt316 =& $this->_buffer;
// 		goto L342;
// 	L341:
// 		$TMIt316 = $this->_buffer;
// 		goto L342;
// 	L342:
// 		$TLE201 = substr($TMIt316, $startPos, $endPos);
// 		return $TLE201;
// 		goto L345;
// 	L345:
// 	}
// 	public function getMemoryLength()
// 	{
// 		$TSt202 = $this->_pos;
// 		return $TSt202;
// 	}
// 	public function setMemorySize($size)
// 	{
// 		$TSt203 = $this->_pos;
// 		$TLE204 = ($TSt203 < $size);
// 		if (TLE204) goto L360 else goto L361;
// 	L360:
// 		$TLE205 = 0;
// 		$TLE206 = ($TLE205 < $size);
// 		if (TLE206) goto L354 else goto L355;
// 	L354:
// 		$TSt207 = $this->_pos;
// 		$pos = $TSt207;
// 		$i = 0;
// 		$ElcfPF1 = True;
// 	L353:
// 		if (ElcfPF1) goto L346 else goto L347;
// 	L346:
// 		$ElcfPF1 = False;
// 		goto L348;
// 	L347:
// 		++$i;
// 		goto L348;
// 	L348:
// 		$TLE208 = ($size - $pos);
// 		$TLE209 = ($i < $TLE208);
// 		if (TLE209) goto L350 else goto L351;
// 	L350:
// 		goto L352;
// 	L351:
// 		goto L349;
// 		goto L352;
// 	L352:
// 		$TLE210 = 255;
// 		$this->addByte($TLE210);
// 		goto L353;
// 	L349:
// 		goto L356;
// 	L355:
// 		goto L356;
// 	L356:
// 		goto L362;
// 	L361:
// 		$TLE211 = 0;
// 		$TLE319 = param_is_ref (NULL, "substr", 0);
// 		if (TLE319) goto L357 else goto L358;
// 	L357:
// 		$TMIt318 =& $this->_buffer;
// 		goto L359;
// 	L358:
// 		$TMIt318 = $this->_buffer;
// 		goto L359;
// 	L359:
// 		$TLE212 = substr($TMIt318, $TLE211, $size);
// 		$this->_buffer = $TLE212;
// 		$this->_pos = $size;
// 		goto L362;
// 	L362:
// 	}
// 	public function resetMemory()
// 	{
// 		$TLE213 = '';
// 		$this->_buffer = $TLE213;
// 		$TLE214 = '';
// 		$this->_mem = $TLE214;
// 		$TLE215 = 0;
// 		$this->_pos = $TLE215;
// 		$TLE216 = 0;
// 		$this->_readPos = $TLE216;
// 	}
// 	public function dumpMemory()
// 	{
// 		$i = 0;
// 		$ElcfPF2 = True;
// 	L376:
// 		if (ElcfPF2) goto L363 else goto L364;
// 	L363:
// 		$ElcfPF2 = False;
// 		goto L365;
// 	L364:
// 		++$i;
// 		goto L365;
// 	L365:
// 		$TSt217 = $this->_pos;
// 		$TLE218 = ($i < $TSt217);
// 		if (TLE218) goto L367 else goto L368;
// 	L367:
// 		goto L369;
// 	L368:
// 		goto L366;
// 		goto L369;
// 	L369:
// 		$TLE219 = '%02X ';
// 		$TLE322 = param_is_ref (NULL, "printf", 0);
// 		if (TLE322) goto L370 else goto L371;
// 	L370:
// 		$TMIt320 =& $this->_mem;
// 		$TMIi321 =& $TMIt320[$i];
// 		goto L372;
// 	L371:
// 		$TMIt320 = $this->_mem;
// 		$TMIi321 = $TMIt320[$i];
// 		goto L372;
// 	L372:
// 		printf($TLE219, $TMIi321);
// 		$TLE220 = 1;
// 		$TLE221 = ($i + $TLE220);
// 		$TLE222 = 50;
// 		$TLE223 = ($TLE221 % $TLE222);
// 		$TLE224 = 0;
// 		$TLE225 = ($TLE223 == $TLE224);
// 		if (TLE225) goto L373 else goto L374;
// 	L373:
// 		$TLE226 = '
// ';
// 		printf($TLE226);
// 		goto L375;
// 	L374:
// 		goto L375;
// 	L375:
// 		goto L376;
// 	L366:
// 		$TLE227 = 1;
// 		$TLE228 = ($i + $TLE227);
// 		$TLE229 = 50;
// 		$TLE230 = ($TLE228 % $TLE229);
// 		$TLE231 = 0;
// 		$TLE232 = ($TLE230 != $TLE231);
// 		if (TLE232) goto L377 else goto L378;
// 	L377:
// 		$TLE233 = '
// ';
// 		printf($TLE233);
// 		goto L379;
// 	L378:
// 		goto L379;
// 	L379:
// 	}
// }
// public function __construct($memorySize = 0)
// {
// 	$this->setMemorySize($memorySize);
// }
PHP_METHOD(Memory, __construct)
{
zval* local_memorySize = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
if (num_args <= 0)
{
zval* default_value;
{
zval* local___static_value__ = NULL;
// $__static_value__ = 0;
{
        if (local___static_value__ == NULL)
    {
      local___static_value__ = EG (uninitialized_zval_ptr);
      local___static_value__->refcount++;
    }
  zval** p_lhs = &local___static_value__;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
default_value = local___static_value__;
assert(!default_value->is_ref);
default_value->refcount++;
if (local___static_value__ != NULL)
{
zval_ptr_dtor (&local___static_value__);
}
}
default_value->refcount--;
	params[0] = default_value;
}
params[0]->refcount++;
if (local_memorySize != NULL)
{
	zval_ptr_dtor (&local_memorySize);
}
local_memorySize = params[0];
}
// Function body
// $this->setMemorySize($memorySize);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "setMemorySize", "tools.source.php", 69 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_memorySize == NULL)
    {
      local_memorySize = EG (uninitialized_zval_ptr);
      local_memorySize->refcount++;
    }
  zval** p_arg = &local_memorySize;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_memorySize == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_memorySize;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 69, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_memorySize != NULL)
{
zval_ptr_dtor (&local_memorySize);
}
}
// public function addByte($byte)
// {
// 	$TSt51 =& $this->_buffer;
// 	$TLE52 = 255;
// 	$TLE53 = ($byte & $TLE52);
// 	$TLE54 = chr($TLE53);
// 	$TSt51 = ($TSt51 . $TLE54);
// 	$TSt55 = $this->_pos;
// 	$TSt56 =& $this->_mem;
// 	$TLE57 = 255;
// 	$TLE58 = ($byte & $TLE57);
// 	$TSt56[$TSt55] = $TLE58;
// 	$TSt59 =& $this->_pos;
// 	++$TSt59;
// }
PHP_METHOD(Memory, addByte)
{
zval* local_TLE52 = NULL;
zval* local_TLE53 = NULL;
zval* local_TLE54 = NULL;
zval* local_TLE57 = NULL;
zval* local_TLE58 = NULL;
zval* local_TSt51 = NULL;
zval* local_TSt55 = NULL;
zval* local_TSt56 = NULL;
zval* local_TSt59 = NULL;
zval* local_byte = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_byte != NULL)
{
	zval_ptr_dtor (&local_byte);
}
local_byte = params[0];
}
// Function body
// $TSt51 =& $this->_buffer;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_buffer", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TSt51 == NULL)
    {
      local_TSt51 = EG (uninitialized_zval_ptr);
      local_TSt51->refcount++;
    }
  zval** p_lhs = &local_TSt51;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// $TLE52 = 255;
{
        if (local_TLE52 == NULL)
    {
      local_TLE52 = EG (uninitialized_zval_ptr);
      local_TLE52->refcount++;
    }
  zval** p_lhs = &local_TLE52;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE53 = ($byte & $TLE52);
{
    if (local_TLE53 == NULL)
    {
      local_TLE53 = EG (uninitialized_zval_ptr);
      local_TLE53->refcount++;
    }
  zval** p_lhs = &local_TLE53;

    zval* left;
  if (local_byte == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_byte;

    zval* right;
  if (local_TLE52 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE52;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE54 = chr($TLE53);
{
   initialize_function_call (&chr_fci, &chr_fcic, "chr", "tools.source.php", 72 TSRMLS_CC);
      zend_function* signature = chr_fcic.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE53 == NULL)
    {
      local_TLE53 = EG (uninitialized_zval_ptr);
      local_TLE53->refcount++;
    }
  zval** p_arg = &local_TLE53;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE53 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE53;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 72, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = chr_fci.param_count;
   zval*** params_save = chr_fci.params;
   zval** retval_save = chr_fci.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   chr_fci.params = args_ind;
   chr_fci.param_count = 1;
   chr_fci.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&chr_fci, &chr_fcic TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   chr_fci.params = params_save;
   chr_fci.param_count = param_count_save;
   chr_fci.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE54 == NULL)
    {
      local_TLE54 = EG (uninitialized_zval_ptr);
      local_TLE54->refcount++;
    }
  zval** p_lhs = &local_TLE54;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TSt51 = ($TSt51 . $TLE54);
{
    if (local_TSt51 == NULL)
    {
      local_TSt51 = EG (uninitialized_zval_ptr);
      local_TSt51->refcount++;
    }
  zval** p_lhs = &local_TSt51;

    zval* left;
  if (local_TSt51 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt51;

    zval* right;
  if (local_TLE54 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE54;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  concat_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TSt55 = $this->_pos;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_pos", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt55 == NULL)
    {
      local_TSt55 = EG (uninitialized_zval_ptr);
      local_TSt55->refcount++;
    }
  zval** p_lhs = &local_TSt55;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TSt56 =& $this->_mem;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_mem", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TSt56 == NULL)
    {
      local_TSt56 = EG (uninitialized_zval_ptr);
      local_TSt56->refcount++;
    }
  zval** p_lhs = &local_TSt56;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// $TLE57 = 255;
{
        if (local_TLE57 == NULL)
    {
      local_TLE57 = EG (uninitialized_zval_ptr);
      local_TLE57->refcount++;
    }
  zval** p_lhs = &local_TLE57;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE58 = ($byte & $TLE57);
{
    if (local_TLE58 == NULL)
    {
      local_TLE58 = EG (uninitialized_zval_ptr);
      local_TLE58->refcount++;
    }
  zval** p_lhs = &local_TLE58;

    zval* left;
  if (local_byte == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_byte;

    zval* right;
  if (local_TLE57 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE57;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TSt56[$TSt55] = $TLE58;
{
     if (local_TSt56 == NULL)
    {
      local_TSt56 = EG (uninitialized_zval_ptr);
      local_TSt56->refcount++;
    }
  zval** p_array = &local_TSt56;

   check_array_type (p_array TSRMLS_CC);

     zval* index;
  if (local_TSt55 == NULL)
    index = EG (uninitialized_zval_ptr);
  else
    index = local_TSt55;


   // String indexing
   if (Z_TYPE_PP (p_array) == IS_STRING && Z_STRLEN_PP (p_array) > 0)
   {
        zval* rhs;
  if (local_TLE58 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE58;

      write_string_index (p_array, index, rhs TSRMLS_CC);
   }
   else if (Z_TYPE_PP (p_array) == IS_ARRAY)
   {
      zval** p_lhs = get_ht_entry (p_array, index TSRMLS_CC);
        zval* rhs;
  if (local_TLE58 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE58;

      if (*p_lhs != rhs)
      {
	 write_var (p_lhs, rhs);
      }
   }
phc_check_invariants (TSRMLS_C);
}
// $TSt59 =& $this->_pos;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_pos", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TSt59 == NULL)
    {
      local_TSt59 = EG (uninitialized_zval_ptr);
      local_TSt59->refcount++;
    }
  zval** p_lhs = &local_TSt59;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// ++$TSt59;
{
     if (local_TSt59 == NULL)
    {
      local_TSt59 = EG (uninitialized_zval_ptr);
      local_TSt59->refcount++;
    }
  zval** p_var = &local_TSt59;

   sep_copy_on_write (p_var);
   increment_function (*p_var);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE52 != NULL)
{
zval_ptr_dtor (&local_TLE52);
}
if (local_TLE53 != NULL)
{
zval_ptr_dtor (&local_TLE53);
}
if (local_TLE54 != NULL)
{
zval_ptr_dtor (&local_TLE54);
}
if (local_TLE57 != NULL)
{
zval_ptr_dtor (&local_TLE57);
}
if (local_TLE58 != NULL)
{
zval_ptr_dtor (&local_TLE58);
}
if (local_TSt51 != NULL)
{
zval_ptr_dtor (&local_TSt51);
}
if (local_TSt55 != NULL)
{
zval_ptr_dtor (&local_TSt55);
}
if (local_TSt56 != NULL)
{
zval_ptr_dtor (&local_TSt56);
}
if (local_TSt59 != NULL)
{
zval_ptr_dtor (&local_TSt59);
}
if (local_byte != NULL)
{
zval_ptr_dtor (&local_byte);
}
}
// public function addString($string)
// {
// 	$i = 0;
// 	$ElcfPF0 = True;
// L333:
// 	if (ElcfPF0) goto L323 else goto L324;
// L323:
// 	$ElcfPF0 = False;
// 	goto L325;
// L324:
// 	++$i;
// 	goto L325;
// L325:
// 	$TLE60 = strlen($string);
// 	$TLE61 = ($i < $TLE60);
// 	if (TLE61) goto L327 else goto L328;
// L327:
// 	goto L329;
// L328:
// 	goto L326;
// 	goto L329;
// L329:
// 	$TLE315 = param_is_ref (NULL, "ord", 0);
// 	if (TLE315) goto L330 else goto L331;
// L330:
// 	$TMIi314 =& $string[$i];
// 	goto L332;
// L331:
// 	$TMIi314 = $string[$i];
// 	goto L332;
// L332:
// 	$TLE62 = ord($TMIi314);
// 	$this->addByte($TLE62);
// 	goto L333;
// L326:
// }
PHP_METHOD(Memory, addString)
{
zval* local_ElcfPF0 = NULL;
zval* local_TLE315 = NULL;
zval* local_TLE60 = NULL;
zval* local_TLE61 = NULL;
zval* local_TLE62 = NULL;
zval* local_TMIi314 = NULL;
zval* local_i = NULL;
zval* local_string = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_string != NULL)
{
	zval_ptr_dtor (&local_string);
}
local_string = params[0];
}
// Function body
// $i = 0;
{
        if (local_i == NULL)
    {
      local_i = EG (uninitialized_zval_ptr);
      local_i->refcount++;
    }
  zval** p_lhs = &local_i;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $ElcfPF0 = True;
{
        if (local_ElcfPF0 == NULL)
    {
      local_ElcfPF0 = EG (uninitialized_zval_ptr);
      local_ElcfPF0->refcount++;
    }
  zval** p_lhs = &local_ElcfPF0;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_BOOL (value, 1);

phc_check_invariants (TSRMLS_C);
}
// L333:
L333:;
// if (ElcfPF0) goto L323 else goto L324;
{
     zval* p_cond;
  if (local_ElcfPF0 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_ElcfPF0;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L323;
   else
      goto L324;
phc_check_invariants (TSRMLS_C);
}
// L323:
L323:;
// $ElcfPF0 = False;
{
        if (local_ElcfPF0 == NULL)
    {
      local_ElcfPF0 = EG (uninitialized_zval_ptr);
      local_ElcfPF0->refcount++;
    }
  zval** p_lhs = &local_ElcfPF0;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_BOOL (value, 0);

phc_check_invariants (TSRMLS_C);
}
// goto L325;
{
goto L325;
phc_check_invariants (TSRMLS_C);
}
// L324:
L324:;
// ++$i;
{
     if (local_i == NULL)
    {
      local_i = EG (uninitialized_zval_ptr);
      local_i->refcount++;
    }
  zval** p_var = &local_i;

   sep_copy_on_write (p_var);
   increment_function (*p_var);
phc_check_invariants (TSRMLS_C);
}
// goto L325;
{
goto L325;
phc_check_invariants (TSRMLS_C);
}
// L325:
L325:;
// $TLE60 = strlen($string);
{
   initialize_function_call (&strlen_fci, &strlen_fcic, "strlen", "tools.source.php", 77 TSRMLS_CC);
      zend_function* signature = strlen_fcic.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_string == NULL)
    {
      local_string = EG (uninitialized_zval_ptr);
      local_string->refcount++;
    }
  zval** p_arg = &local_string;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_string == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_string;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 77, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = strlen_fci.param_count;
   zval*** params_save = strlen_fci.params;
   zval** retval_save = strlen_fci.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   strlen_fci.params = args_ind;
   strlen_fci.param_count = 1;
   strlen_fci.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&strlen_fci, &strlen_fcic TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   strlen_fci.params = params_save;
   strlen_fci.param_count = param_count_save;
   strlen_fci.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE60 == NULL)
    {
      local_TLE60 = EG (uninitialized_zval_ptr);
      local_TLE60->refcount++;
    }
  zval** p_lhs = &local_TLE60;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE61 = ($i < $TLE60);
{
    if (local_TLE61 == NULL)
    {
      local_TLE61 = EG (uninitialized_zval_ptr);
      local_TLE61->refcount++;
    }
  zval** p_lhs = &local_TLE61;

    zval* left;
  if (local_i == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_i;

    zval* right;
  if (local_TLE60 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE60;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_smaller_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE61) goto L327 else goto L328;
{
     zval* p_cond;
  if (local_TLE61 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE61;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L327;
   else
      goto L328;
phc_check_invariants (TSRMLS_C);
}
// L327:
L327:;
// goto L329;
{
goto L329;
phc_check_invariants (TSRMLS_C);
}
// L328:
L328:;
// goto L326;
{
goto L326;
phc_check_invariants (TSRMLS_C);
}
// goto L329;
{
goto L329;
phc_check_invariants (TSRMLS_C);
}
// L329:
L329:;
// $TLE315 = param_is_ref (NULL, "ord", 0);
{
   initialize_function_call (&ord_fci, &ord_fcic, "ord", "<unknown>", 0 TSRMLS_CC);
		zend_function* signature = ord_fcic.function_handler;
	zend_arg_info* arg_info = signature->common.arg_info;
	int count = 0;
	while (arg_info && count < 0)
	{
		count++;
		arg_info++;
	}

	  if (local_TLE315 == NULL)
    {
      local_TLE315 = EG (uninitialized_zval_ptr);
      local_TLE315->refcount++;
    }
  zval** p_lhs = &local_TLE315;

	zval* rhs;
	ALLOC_INIT_ZVAL (rhs);
	if (arg_info && count == 0)
	{
		ZVAL_BOOL (rhs, arg_info->pass_by_reference);
	}
	else
	{
		ZVAL_BOOL (rhs, signature->common.pass_rest_by_reference);
	}
	write_var (p_lhs, rhs);
	zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// if (TLE315) goto L330 else goto L331;
{
     zval* p_cond;
  if (local_TLE315 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE315;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L330;
   else
      goto L331;
phc_check_invariants (TSRMLS_C);
}
// L330:
L330:;
// $TMIi314 =& $string[$i];
{
     if (local_TMIi314 == NULL)
    {
      local_TMIi314 = EG (uninitialized_zval_ptr);
      local_TMIi314->refcount++;
    }
  zval** p_lhs = &local_TMIi314;

     if (local_string == NULL)
    {
      local_string = EG (uninitialized_zval_ptr);
      local_string->refcount++;
    }
  zval** p_r_array = &local_string;

     zval* r_index;
  if (local_i == NULL)
    r_index = EG (uninitialized_zval_ptr);
  else
    r_index = local_i;

   check_array_type (p_r_array TSRMLS_CC);
   zval** p_rhs = get_ht_entry (p_r_array, r_index TSRMLS_CC);
   sep_copy_on_write (p_rhs);
   copy_into_ref (p_lhs, p_rhs);
phc_check_invariants (TSRMLS_C);
}
// goto L332;
{
goto L332;
phc_check_invariants (TSRMLS_C);
}
// L331:
L331:;
// $TMIi314 = $string[$i];
{
     if (local_TMIi314 == NULL)
    {
      local_TMIi314 = EG (uninitialized_zval_ptr);
      local_TMIi314->refcount++;
    }
  zval** p_lhs = &local_TMIi314;

     zval* r_array;
  if (local_string == NULL)
    r_array = EG (uninitialized_zval_ptr);
  else
    r_array = local_string;

     zval* r_index;
  if (local_i == NULL)
    r_index = EG (uninitialized_zval_ptr);
  else
    r_index = local_i;


   zval* rhs;
   int is_rhs_new = 0;
    if (Z_TYPE_P (r_array) != IS_ARRAY)
    {
      if (Z_TYPE_P (r_array) == IS_STRING)
	{
	  is_rhs_new = 1;
	  rhs = read_string_index (r_array, r_index TSRMLS_CC);
	}
      else
	// TODO: warning here?
	rhs = EG (uninitialized_zval_ptr);
    }
    else
    {
      if (check_array_index_type (r_index TSRMLS_CC))
	{
	  // Read array variable
	  read_array (&rhs, r_array, r_index TSRMLS_CC);
	}
      else
	rhs = *p_lhs; // HACK to fail  *p_lhs != rhs
    }

   if (*p_lhs != rhs)
      write_var (p_lhs, rhs);

   if (is_rhs_new) zval_ptr_dtor (&rhs);
phc_check_invariants (TSRMLS_C);
}
// goto L332;
{
goto L332;
phc_check_invariants (TSRMLS_C);
}
// L332:
L332:;
// $TLE62 = ord($TMIi314);
{
   initialize_function_call (&ord_fci, &ord_fcic, "ord", "tools.source.php", 78 TSRMLS_CC);
      zend_function* signature = ord_fcic.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TMIi314 == NULL)
    {
      local_TMIi314 = EG (uninitialized_zval_ptr);
      local_TMIi314->refcount++;
    }
  zval** p_arg = &local_TMIi314;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TMIi314 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TMIi314;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 78, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = ord_fci.param_count;
   zval*** params_save = ord_fci.params;
   zval** retval_save = ord_fci.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   ord_fci.params = args_ind;
   ord_fci.param_count = 1;
   ord_fci.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&ord_fci, &ord_fcic TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   ord_fci.params = params_save;
   ord_fci.param_count = param_count_save;
   ord_fci.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE62 == NULL)
    {
      local_TLE62 = EG (uninitialized_zval_ptr);
      local_TLE62->refcount++;
    }
  zval** p_lhs = &local_TLE62;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $this->addByte($TLE62);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "addByte", "tools.source.php", 78 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE62 == NULL)
    {
      local_TLE62 = EG (uninitialized_zval_ptr);
      local_TLE62->refcount++;
    }
  zval** p_arg = &local_TLE62;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE62 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE62;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 78, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// goto L333;
{
goto L333;
phc_check_invariants (TSRMLS_C);
}
// L326:
L326:;
// Method exit
end_of_function:__attribute__((unused));
if (local_ElcfPF0 != NULL)
{
zval_ptr_dtor (&local_ElcfPF0);
}
if (local_TLE315 != NULL)
{
zval_ptr_dtor (&local_TLE315);
}
if (local_TLE60 != NULL)
{
zval_ptr_dtor (&local_TLE60);
}
if (local_TLE61 != NULL)
{
zval_ptr_dtor (&local_TLE61);
}
if (local_TLE62 != NULL)
{
zval_ptr_dtor (&local_TLE62);
}
if (local_TMIi314 != NULL)
{
zval_ptr_dtor (&local_TMIi314);
}
if (local_i != NULL)
{
zval_ptr_dtor (&local_i);
}
if (local_string != NULL)
{
zval_ptr_dtor (&local_string);
}
}
// public function addShort($short)
// {
// 	$TLE63 = 65535;
// 	$short = ($short & $TLE63);
// 	$TLE64 = 8;
// 	$TLE65 = ($short >> $TLE64);
// 	$this->addByte($TLE65);
// 	$TLE66 = 8;
// 	$TLE67 = ($short >> $TLE66);
// 	$TLE68 = 8;
// 	$TLE69 = ($TLE67 << $TLE68);
// 	$short = ($short - $TLE69);
// 	$this->addByte($short);
// }
PHP_METHOD(Memory, addShort)
{
zval* local_TLE63 = NULL;
zval* local_TLE64 = NULL;
zval* local_TLE65 = NULL;
zval* local_TLE66 = NULL;
zval* local_TLE67 = NULL;
zval* local_TLE68 = NULL;
zval* local_TLE69 = NULL;
zval* local_short = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_short != NULL)
{
	zval_ptr_dtor (&local_short);
}
local_short = params[0];
}
// Function body
// $TLE63 = 65535;
{
        if (local_TLE63 == NULL)
    {
      local_TLE63 = EG (uninitialized_zval_ptr);
      local_TLE63->refcount++;
    }
  zval** p_lhs = &local_TLE63;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65535);

phc_check_invariants (TSRMLS_C);
}
// $short = ($short & $TLE63);
{
    if (local_short == NULL)
    {
      local_short = EG (uninitialized_zval_ptr);
      local_short->refcount++;
    }
  zval** p_lhs = &local_short;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE63 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE63;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE64 = 8;
{
        if (local_TLE64 == NULL)
    {
      local_TLE64 = EG (uninitialized_zval_ptr);
      local_TLE64->refcount++;
    }
  zval** p_lhs = &local_TLE64;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE65 = ($short >> $TLE64);
{
    if (local_TLE65 == NULL)
    {
      local_TLE65 = EG (uninitialized_zval_ptr);
      local_TLE65->refcount++;
    }
  zval** p_lhs = &local_TLE65;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE64 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE64;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->addByte($TLE65);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "addByte", "tools.source.php", 83 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE65 == NULL)
    {
      local_TLE65 = EG (uninitialized_zval_ptr);
      local_TLE65->refcount++;
    }
  zval** p_arg = &local_TLE65;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE65 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE65;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 83, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE66 = 8;
{
        if (local_TLE66 == NULL)
    {
      local_TLE66 = EG (uninitialized_zval_ptr);
      local_TLE66->refcount++;
    }
  zval** p_lhs = &local_TLE66;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE67 = ($short >> $TLE66);
{
    if (local_TLE67 == NULL)
    {
      local_TLE67 = EG (uninitialized_zval_ptr);
      local_TLE67->refcount++;
    }
  zval** p_lhs = &local_TLE67;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE66 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE66;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE68 = 8;
{
        if (local_TLE68 == NULL)
    {
      local_TLE68 = EG (uninitialized_zval_ptr);
      local_TLE68->refcount++;
    }
  zval** p_lhs = &local_TLE68;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE69 = ($TLE67 << $TLE68);
{
    if (local_TLE69 == NULL)
    {
      local_TLE69 = EG (uninitialized_zval_ptr);
      local_TLE69->refcount++;
    }
  zval** p_lhs = &local_TLE69;

    zval* left;
  if (local_TLE67 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE67;

    zval* right;
  if (local_TLE68 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE68;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $short = ($short - $TLE69);
{
    if (local_short == NULL)
    {
      local_short = EG (uninitialized_zval_ptr);
      local_short->refcount++;
    }
  zval** p_lhs = &local_short;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE69 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE69;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->addByte($short);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "addByte", "tools.source.php", 85 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_short == NULL)
    {
      local_short = EG (uninitialized_zval_ptr);
      local_short->refcount++;
    }
  zval** p_arg = &local_short;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_short == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_short;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 85, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE63 != NULL)
{
zval_ptr_dtor (&local_TLE63);
}
if (local_TLE64 != NULL)
{
zval_ptr_dtor (&local_TLE64);
}
if (local_TLE65 != NULL)
{
zval_ptr_dtor (&local_TLE65);
}
if (local_TLE66 != NULL)
{
zval_ptr_dtor (&local_TLE66);
}
if (local_TLE67 != NULL)
{
zval_ptr_dtor (&local_TLE67);
}
if (local_TLE68 != NULL)
{
zval_ptr_dtor (&local_TLE68);
}
if (local_TLE69 != NULL)
{
zval_ptr_dtor (&local_TLE69);
}
if (local_short != NULL)
{
zval_ptr_dtor (&local_short);
}
}
// public function addInteger($int)
// {
// 	$TLE70 = 4294967295.0;
// 	$int = ($int & $TLE70);
// 	$TLE71 = 24;
// 	$TLE72 = ($int >> $TLE71);
// 	$TLE73 = 255;
// 	$TLE74 = ($TLE72 & $TLE73);
// 	$this->addByte($TLE74);
// 	$TLE75 = 24;
// 	$TLE76 = ($int >> $TLE75);
// 	$TLE77 = 255;
// 	$TLE78 = ($TLE76 & $TLE77);
// 	$TLE79 = 24;
// 	$TLE80 = ($TLE78 << $TLE79);
// 	$int = ($int - $TLE80);
// 	$TLE81 = 16;
// 	$TLE82 = ($int >> $TLE81);
// 	$TLE83 = 255;
// 	$TLE84 = ($TLE82 & $TLE83);
// 	$this->addByte($TLE84);
// 	$TLE85 = 16;
// 	$TLE86 = ($int >> $TLE85);
// 	$TLE87 = 255;
// 	$TLE88 = ($TLE86 & $TLE87);
// 	$TLE89 = 16;
// 	$TLE90 = ($TLE88 << $TLE89);
// 	$int = ($int - $TLE90);
// 	$TLE91 = 8;
// 	$TLE92 = ($int >> $TLE91);
// 	$TLE93 = 255;
// 	$TLE94 = ($TLE92 & $TLE93);
// 	$this->addByte($TLE94);
// 	$TLE95 = 8;
// 	$TLE96 = ($int >> $TLE95);
// 	$TLE97 = 255;
// 	$TLE98 = ($TLE96 & $TLE97);
// 	$TLE99 = 8;
// 	$TLE100 = ($TLE98 << $TLE99);
// 	$int = ($int - $TLE100);
// 	$this->addByte($int);
// }
PHP_METHOD(Memory, addInteger)
{
zval* local_TLE100 = NULL;
zval* local_TLE70 = NULL;
zval* local_TLE71 = NULL;
zval* local_TLE72 = NULL;
zval* local_TLE73 = NULL;
zval* local_TLE74 = NULL;
zval* local_TLE75 = NULL;
zval* local_TLE76 = NULL;
zval* local_TLE77 = NULL;
zval* local_TLE78 = NULL;
zval* local_TLE79 = NULL;
zval* local_TLE80 = NULL;
zval* local_TLE81 = NULL;
zval* local_TLE82 = NULL;
zval* local_TLE83 = NULL;
zval* local_TLE84 = NULL;
zval* local_TLE85 = NULL;
zval* local_TLE86 = NULL;
zval* local_TLE87 = NULL;
zval* local_TLE88 = NULL;
zval* local_TLE89 = NULL;
zval* local_TLE90 = NULL;
zval* local_TLE91 = NULL;
zval* local_TLE92 = NULL;
zval* local_TLE93 = NULL;
zval* local_TLE94 = NULL;
zval* local_TLE95 = NULL;
zval* local_TLE96 = NULL;
zval* local_TLE97 = NULL;
zval* local_TLE98 = NULL;
zval* local_TLE99 = NULL;
zval* local_int = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_int != NULL)
{
	zval_ptr_dtor (&local_int);
}
local_int = params[0];
}
// Function body
// $TLE70 = 4294967295.0;
{
        if (local_TLE70 == NULL)
    {
      local_TLE70 = EG (uninitialized_zval_ptr);
      local_TLE70->refcount++;
    }
  zval** p_lhs = &local_TLE70;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   {
	unsigned char val[] = {
0, 0, 224, 255, 255, 255, 239, 65, };
ZVAL_DOUBLE (value, *(double*)(val));
}

phc_check_invariants (TSRMLS_C);
}
// $int = ($int & $TLE70);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE70 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE70;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE71 = 24;
{
        if (local_TLE71 == NULL)
    {
      local_TLE71 = EG (uninitialized_zval_ptr);
      local_TLE71->refcount++;
    }
  zval** p_lhs = &local_TLE71;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 24);

phc_check_invariants (TSRMLS_C);
}
// $TLE72 = ($int >> $TLE71);
{
    if (local_TLE72 == NULL)
    {
      local_TLE72 = EG (uninitialized_zval_ptr);
      local_TLE72->refcount++;
    }
  zval** p_lhs = &local_TLE72;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE71 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE71;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE73 = 255;
{
        if (local_TLE73 == NULL)
    {
      local_TLE73 = EG (uninitialized_zval_ptr);
      local_TLE73->refcount++;
    }
  zval** p_lhs = &local_TLE73;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE74 = ($TLE72 & $TLE73);
{
    if (local_TLE74 == NULL)
    {
      local_TLE74 = EG (uninitialized_zval_ptr);
      local_TLE74->refcount++;
    }
  zval** p_lhs = &local_TLE74;

    zval* left;
  if (local_TLE72 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE72;

    zval* right;
  if (local_TLE73 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE73;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->addByte($TLE74);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "addByte", "tools.source.php", 89 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE74 == NULL)
    {
      local_TLE74 = EG (uninitialized_zval_ptr);
      local_TLE74->refcount++;
    }
  zval** p_arg = &local_TLE74;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE74 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE74;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 89, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE75 = 24;
{
        if (local_TLE75 == NULL)
    {
      local_TLE75 = EG (uninitialized_zval_ptr);
      local_TLE75->refcount++;
    }
  zval** p_lhs = &local_TLE75;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 24);

phc_check_invariants (TSRMLS_C);
}
// $TLE76 = ($int >> $TLE75);
{
    if (local_TLE76 == NULL)
    {
      local_TLE76 = EG (uninitialized_zval_ptr);
      local_TLE76->refcount++;
    }
  zval** p_lhs = &local_TLE76;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE75 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE75;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE77 = 255;
{
        if (local_TLE77 == NULL)
    {
      local_TLE77 = EG (uninitialized_zval_ptr);
      local_TLE77->refcount++;
    }
  zval** p_lhs = &local_TLE77;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE78 = ($TLE76 & $TLE77);
{
    if (local_TLE78 == NULL)
    {
      local_TLE78 = EG (uninitialized_zval_ptr);
      local_TLE78->refcount++;
    }
  zval** p_lhs = &local_TLE78;

    zval* left;
  if (local_TLE76 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE76;

    zval* right;
  if (local_TLE77 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE77;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE79 = 24;
{
        if (local_TLE79 == NULL)
    {
      local_TLE79 = EG (uninitialized_zval_ptr);
      local_TLE79->refcount++;
    }
  zval** p_lhs = &local_TLE79;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 24);

phc_check_invariants (TSRMLS_C);
}
// $TLE80 = ($TLE78 << $TLE79);
{
    if (local_TLE80 == NULL)
    {
      local_TLE80 = EG (uninitialized_zval_ptr);
      local_TLE80->refcount++;
    }
  zval** p_lhs = &local_TLE80;

    zval* left;
  if (local_TLE78 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE78;

    zval* right;
  if (local_TLE79 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE79;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $int = ($int - $TLE80);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE80 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE80;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE81 = 16;
{
        if (local_TLE81 == NULL)
    {
      local_TLE81 = EG (uninitialized_zval_ptr);
      local_TLE81->refcount++;
    }
  zval** p_lhs = &local_TLE81;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 16);

phc_check_invariants (TSRMLS_C);
}
// $TLE82 = ($int >> $TLE81);
{
    if (local_TLE82 == NULL)
    {
      local_TLE82 = EG (uninitialized_zval_ptr);
      local_TLE82->refcount++;
    }
  zval** p_lhs = &local_TLE82;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE81 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE81;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE83 = 255;
{
        if (local_TLE83 == NULL)
    {
      local_TLE83 = EG (uninitialized_zval_ptr);
      local_TLE83->refcount++;
    }
  zval** p_lhs = &local_TLE83;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE84 = ($TLE82 & $TLE83);
{
    if (local_TLE84 == NULL)
    {
      local_TLE84 = EG (uninitialized_zval_ptr);
      local_TLE84->refcount++;
    }
  zval** p_lhs = &local_TLE84;

    zval* left;
  if (local_TLE82 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE82;

    zval* right;
  if (local_TLE83 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE83;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->addByte($TLE84);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "addByte", "tools.source.php", 91 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE84 == NULL)
    {
      local_TLE84 = EG (uninitialized_zval_ptr);
      local_TLE84->refcount++;
    }
  zval** p_arg = &local_TLE84;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE84 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE84;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 91, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE85 = 16;
{
        if (local_TLE85 == NULL)
    {
      local_TLE85 = EG (uninitialized_zval_ptr);
      local_TLE85->refcount++;
    }
  zval** p_lhs = &local_TLE85;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 16);

phc_check_invariants (TSRMLS_C);
}
// $TLE86 = ($int >> $TLE85);
{
    if (local_TLE86 == NULL)
    {
      local_TLE86 = EG (uninitialized_zval_ptr);
      local_TLE86->refcount++;
    }
  zval** p_lhs = &local_TLE86;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE85 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE85;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE87 = 255;
{
        if (local_TLE87 == NULL)
    {
      local_TLE87 = EG (uninitialized_zval_ptr);
      local_TLE87->refcount++;
    }
  zval** p_lhs = &local_TLE87;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE88 = ($TLE86 & $TLE87);
{
    if (local_TLE88 == NULL)
    {
      local_TLE88 = EG (uninitialized_zval_ptr);
      local_TLE88->refcount++;
    }
  zval** p_lhs = &local_TLE88;

    zval* left;
  if (local_TLE86 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE86;

    zval* right;
  if (local_TLE87 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE87;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE89 = 16;
{
        if (local_TLE89 == NULL)
    {
      local_TLE89 = EG (uninitialized_zval_ptr);
      local_TLE89->refcount++;
    }
  zval** p_lhs = &local_TLE89;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 16);

phc_check_invariants (TSRMLS_C);
}
// $TLE90 = ($TLE88 << $TLE89);
{
    if (local_TLE90 == NULL)
    {
      local_TLE90 = EG (uninitialized_zval_ptr);
      local_TLE90->refcount++;
    }
  zval** p_lhs = &local_TLE90;

    zval* left;
  if (local_TLE88 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE88;

    zval* right;
  if (local_TLE89 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE89;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $int = ($int - $TLE90);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE90 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE90;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE91 = 8;
{
        if (local_TLE91 == NULL)
    {
      local_TLE91 = EG (uninitialized_zval_ptr);
      local_TLE91->refcount++;
    }
  zval** p_lhs = &local_TLE91;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE92 = ($int >> $TLE91);
{
    if (local_TLE92 == NULL)
    {
      local_TLE92 = EG (uninitialized_zval_ptr);
      local_TLE92->refcount++;
    }
  zval** p_lhs = &local_TLE92;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE91 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE91;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE93 = 255;
{
        if (local_TLE93 == NULL)
    {
      local_TLE93 = EG (uninitialized_zval_ptr);
      local_TLE93->refcount++;
    }
  zval** p_lhs = &local_TLE93;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE94 = ($TLE92 & $TLE93);
{
    if (local_TLE94 == NULL)
    {
      local_TLE94 = EG (uninitialized_zval_ptr);
      local_TLE94->refcount++;
    }
  zval** p_lhs = &local_TLE94;

    zval* left;
  if (local_TLE92 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE92;

    zval* right;
  if (local_TLE93 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE93;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->addByte($TLE94);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "addByte", "tools.source.php", 93 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE94 == NULL)
    {
      local_TLE94 = EG (uninitialized_zval_ptr);
      local_TLE94->refcount++;
    }
  zval** p_arg = &local_TLE94;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE94 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE94;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 93, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE95 = 8;
{
        if (local_TLE95 == NULL)
    {
      local_TLE95 = EG (uninitialized_zval_ptr);
      local_TLE95->refcount++;
    }
  zval** p_lhs = &local_TLE95;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE96 = ($int >> $TLE95);
{
    if (local_TLE96 == NULL)
    {
      local_TLE96 = EG (uninitialized_zval_ptr);
      local_TLE96->refcount++;
    }
  zval** p_lhs = &local_TLE96;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE95 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE95;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE97 = 255;
{
        if (local_TLE97 == NULL)
    {
      local_TLE97 = EG (uninitialized_zval_ptr);
      local_TLE97->refcount++;
    }
  zval** p_lhs = &local_TLE97;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE98 = ($TLE96 & $TLE97);
{
    if (local_TLE98 == NULL)
    {
      local_TLE98 = EG (uninitialized_zval_ptr);
      local_TLE98->refcount++;
    }
  zval** p_lhs = &local_TLE98;

    zval* left;
  if (local_TLE96 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE96;

    zval* right;
  if (local_TLE97 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE97;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE99 = 8;
{
        if (local_TLE99 == NULL)
    {
      local_TLE99 = EG (uninitialized_zval_ptr);
      local_TLE99->refcount++;
    }
  zval** p_lhs = &local_TLE99;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE100 = ($TLE98 << $TLE99);
{
    if (local_TLE100 == NULL)
    {
      local_TLE100 = EG (uninitialized_zval_ptr);
      local_TLE100->refcount++;
    }
  zval** p_lhs = &local_TLE100;

    zval* left;
  if (local_TLE98 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE98;

    zval* right;
  if (local_TLE99 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE99;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $int = ($int - $TLE100);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE100 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE100;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->addByte($int);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "addByte", "tools.source.php", 95 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_arg = &local_int;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_int == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_int;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 95, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE100 != NULL)
{
zval_ptr_dtor (&local_TLE100);
}
if (local_TLE70 != NULL)
{
zval_ptr_dtor (&local_TLE70);
}
if (local_TLE71 != NULL)
{
zval_ptr_dtor (&local_TLE71);
}
if (local_TLE72 != NULL)
{
zval_ptr_dtor (&local_TLE72);
}
if (local_TLE73 != NULL)
{
zval_ptr_dtor (&local_TLE73);
}
if (local_TLE74 != NULL)
{
zval_ptr_dtor (&local_TLE74);
}
if (local_TLE75 != NULL)
{
zval_ptr_dtor (&local_TLE75);
}
if (local_TLE76 != NULL)
{
zval_ptr_dtor (&local_TLE76);
}
if (local_TLE77 != NULL)
{
zval_ptr_dtor (&local_TLE77);
}
if (local_TLE78 != NULL)
{
zval_ptr_dtor (&local_TLE78);
}
if (local_TLE79 != NULL)
{
zval_ptr_dtor (&local_TLE79);
}
if (local_TLE80 != NULL)
{
zval_ptr_dtor (&local_TLE80);
}
if (local_TLE81 != NULL)
{
zval_ptr_dtor (&local_TLE81);
}
if (local_TLE82 != NULL)
{
zval_ptr_dtor (&local_TLE82);
}
if (local_TLE83 != NULL)
{
zval_ptr_dtor (&local_TLE83);
}
if (local_TLE84 != NULL)
{
zval_ptr_dtor (&local_TLE84);
}
if (local_TLE85 != NULL)
{
zval_ptr_dtor (&local_TLE85);
}
if (local_TLE86 != NULL)
{
zval_ptr_dtor (&local_TLE86);
}
if (local_TLE87 != NULL)
{
zval_ptr_dtor (&local_TLE87);
}
if (local_TLE88 != NULL)
{
zval_ptr_dtor (&local_TLE88);
}
if (local_TLE89 != NULL)
{
zval_ptr_dtor (&local_TLE89);
}
if (local_TLE90 != NULL)
{
zval_ptr_dtor (&local_TLE90);
}
if (local_TLE91 != NULL)
{
zval_ptr_dtor (&local_TLE91);
}
if (local_TLE92 != NULL)
{
zval_ptr_dtor (&local_TLE92);
}
if (local_TLE93 != NULL)
{
zval_ptr_dtor (&local_TLE93);
}
if (local_TLE94 != NULL)
{
zval_ptr_dtor (&local_TLE94);
}
if (local_TLE95 != NULL)
{
zval_ptr_dtor (&local_TLE95);
}
if (local_TLE96 != NULL)
{
zval_ptr_dtor (&local_TLE96);
}
if (local_TLE97 != NULL)
{
zval_ptr_dtor (&local_TLE97);
}
if (local_TLE98 != NULL)
{
zval_ptr_dtor (&local_TLE98);
}
if (local_TLE99 != NULL)
{
zval_ptr_dtor (&local_TLE99);
}
if (local_int != NULL)
{
zval_ptr_dtor (&local_int);
}
}
// public function readByte()
// {
// 	$TSt101 =& $this->_readPos;
// 	$TSt102 = $this->_mem;
// 	$TSi103 = $TSt102[$TSt101];
// 	++$TSt101;
// 	return $TSi103;
// }
PHP_METHOD(Memory, readByte)
{
zval* local_TSi103 = NULL;
zval* local_TSt101 = NULL;
zval* local_TSt102 = NULL;
zval* local_this = getThis();
// Function body
// $TSt101 =& $this->_readPos;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_readPos", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TSt101 == NULL)
    {
      local_TSt101 = EG (uninitialized_zval_ptr);
      local_TSt101->refcount++;
    }
  zval** p_lhs = &local_TSt101;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// $TSt102 = $this->_mem;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_mem", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt102 == NULL)
    {
      local_TSt102 = EG (uninitialized_zval_ptr);
      local_TSt102->refcount++;
    }
  zval** p_lhs = &local_TSt102;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TSi103 = $TSt102[$TSt101];
{
     if (local_TSi103 == NULL)
    {
      local_TSi103 = EG (uninitialized_zval_ptr);
      local_TSi103->refcount++;
    }
  zval** p_lhs = &local_TSi103;

     zval* r_array;
  if (local_TSt102 == NULL)
    r_array = EG (uninitialized_zval_ptr);
  else
    r_array = local_TSt102;

     zval* r_index;
  if (local_TSt101 == NULL)
    r_index = EG (uninitialized_zval_ptr);
  else
    r_index = local_TSt101;


   zval* rhs;
   int is_rhs_new = 0;
    if (Z_TYPE_P (r_array) != IS_ARRAY)
    {
      if (Z_TYPE_P (r_array) == IS_STRING)
	{
	  is_rhs_new = 1;
	  rhs = read_string_index (r_array, r_index TSRMLS_CC);
	}
      else
	// TODO: warning here?
	rhs = EG (uninitialized_zval_ptr);
    }
    else
    {
      if (check_array_index_type (r_index TSRMLS_CC))
	{
	  // Read array variable
	  read_array (&rhs, r_array, r_index TSRMLS_CC);
	}
      else
	rhs = *p_lhs; // HACK to fail  *p_lhs != rhs
    }

   if (*p_lhs != rhs)
      write_var (p_lhs, rhs);

   if (is_rhs_new) zval_ptr_dtor (&rhs);
phc_check_invariants (TSRMLS_C);
}
// ++$TSt101;
{
     if (local_TSt101 == NULL)
    {
      local_TSt101 = EG (uninitialized_zval_ptr);
      local_TSt101->refcount++;
    }
  zval** p_var = &local_TSt101;

   sep_copy_on_write (p_var);
   increment_function (*p_var);
phc_check_invariants (TSRMLS_C);
}
// return $TSi103;
{
     zval* rhs;
  if (local_TSi103 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TSi103;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TSi103 != NULL)
{
zval_ptr_dtor (&local_TSi103);
}
if (local_TSt101 != NULL)
{
zval_ptr_dtor (&local_TSt101);
}
if (local_TSt102 != NULL)
{
zval_ptr_dtor (&local_TSt102);
}
}
// public function readShort()
// {
// 	$TLE104 = $this->readByte();
// 	$TLE105 = 8;
// 	$short = ($TLE104 << $TLE105);
// 	$TLE106 = $this->readByte();
// 	$short = ($short + $TLE106);
// 	return $short;
// }
PHP_METHOD(Memory, readShort)
{
zval* local_TLE104 = NULL;
zval* local_TLE105 = NULL;
zval* local_TLE106 = NULL;
zval* local_short = NULL;
zval* local_this = getThis();
// Function body
// $TLE104 = $this->readByte();
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "readByte", "tools.source.php", 101 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[0];
   int abr_index = 0;
   

   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [0];
   zval* args [0];
   zval** args_ind [0];

   int af_index = 0;
   

   phc_setup_error (1, "tools.source.php", 101, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 0;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 0; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE104 == NULL)
    {
      local_TLE104 = EG (uninitialized_zval_ptr);
      local_TLE104->refcount++;
    }
  zval** p_lhs = &local_TLE104;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE105 = 8;
{
        if (local_TLE105 == NULL)
    {
      local_TLE105 = EG (uninitialized_zval_ptr);
      local_TLE105->refcount++;
    }
  zval** p_lhs = &local_TLE105;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $short = ($TLE104 << $TLE105);
{
    if (local_short == NULL)
    {
      local_short = EG (uninitialized_zval_ptr);
      local_short->refcount++;
    }
  zval** p_lhs = &local_short;

    zval* left;
  if (local_TLE104 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE104;

    zval* right;
  if (local_TLE105 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE105;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE106 = $this->readByte();
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "readByte", "tools.source.php", 102 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[0];
   int abr_index = 0;
   

   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [0];
   zval* args [0];
   zval** args_ind [0];

   int af_index = 0;
   

   phc_setup_error (1, "tools.source.php", 102, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 0;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 0; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE106 == NULL)
    {
      local_TLE106 = EG (uninitialized_zval_ptr);
      local_TLE106->refcount++;
    }
  zval** p_lhs = &local_TLE106;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $short = ($short + $TLE106);
{
    if (local_short == NULL)
    {
      local_short = EG (uninitialized_zval_ptr);
      local_short->refcount++;
    }
  zval** p_lhs = &local_short;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE106 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE106;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// return $short;
{
     zval* rhs;
  if (local_short == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_short;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE104 != NULL)
{
zval_ptr_dtor (&local_TLE104);
}
if (local_TLE105 != NULL)
{
zval_ptr_dtor (&local_TLE105);
}
if (local_TLE106 != NULL)
{
zval_ptr_dtor (&local_TLE106);
}
if (local_short != NULL)
{
zval_ptr_dtor (&local_short);
}
}
// public function readInteger()
// {
// 	$TLE107 = $this->readByte();
// 	$TLE108 = 24;
// 	$int = ($TLE107 << $TLE108);
// 	$TLE109 = $this->readByte();
// 	$TLE110 = 16;
// 	$TLE111 = ($TLE109 << $TLE110);
// 	$int = ($int + $TLE111);
// 	$TLE112 = $this->readByte();
// 	$TLE113 = 8;
// 	$TLE114 = ($TLE112 << $TLE113);
// 	$int = ($int + $TLE114);
// 	$TLE115 = $this->readByte();
// 	$int = ($int + $TLE115);
// 	$TLE116 = (int) $int;
// 	return $TLE116;
// }
PHP_METHOD(Memory, readInteger)
{
zval* local_TLE107 = NULL;
zval* local_TLE108 = NULL;
zval* local_TLE109 = NULL;
zval* local_TLE110 = NULL;
zval* local_TLE111 = NULL;
zval* local_TLE112 = NULL;
zval* local_TLE113 = NULL;
zval* local_TLE114 = NULL;
zval* local_TLE115 = NULL;
zval* local_TLE116 = NULL;
zval* local_int = NULL;
zval* local_this = getThis();
// Function body
// $TLE107 = $this->readByte();
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "readByte", "tools.source.php", 106 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[0];
   int abr_index = 0;
   

   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [0];
   zval* args [0];
   zval** args_ind [0];

   int af_index = 0;
   

   phc_setup_error (1, "tools.source.php", 106, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 0;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 0; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE107 == NULL)
    {
      local_TLE107 = EG (uninitialized_zval_ptr);
      local_TLE107->refcount++;
    }
  zval** p_lhs = &local_TLE107;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE108 = 24;
{
        if (local_TLE108 == NULL)
    {
      local_TLE108 = EG (uninitialized_zval_ptr);
      local_TLE108->refcount++;
    }
  zval** p_lhs = &local_TLE108;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 24);

phc_check_invariants (TSRMLS_C);
}
// $int = ($TLE107 << $TLE108);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_TLE107 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE107;

    zval* right;
  if (local_TLE108 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE108;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE109 = $this->readByte();
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "readByte", "tools.source.php", 107 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[0];
   int abr_index = 0;
   

   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [0];
   zval* args [0];
   zval** args_ind [0];

   int af_index = 0;
   

   phc_setup_error (1, "tools.source.php", 107, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 0;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 0; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE109 == NULL)
    {
      local_TLE109 = EG (uninitialized_zval_ptr);
      local_TLE109->refcount++;
    }
  zval** p_lhs = &local_TLE109;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE110 = 16;
{
        if (local_TLE110 == NULL)
    {
      local_TLE110 = EG (uninitialized_zval_ptr);
      local_TLE110->refcount++;
    }
  zval** p_lhs = &local_TLE110;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 16);

phc_check_invariants (TSRMLS_C);
}
// $TLE111 = ($TLE109 << $TLE110);
{
    if (local_TLE111 == NULL)
    {
      local_TLE111 = EG (uninitialized_zval_ptr);
      local_TLE111->refcount++;
    }
  zval** p_lhs = &local_TLE111;

    zval* left;
  if (local_TLE109 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE109;

    zval* right;
  if (local_TLE110 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE110;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $int = ($int + $TLE111);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE111 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE111;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE112 = $this->readByte();
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "readByte", "tools.source.php", 108 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[0];
   int abr_index = 0;
   

   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [0];
   zval* args [0];
   zval** args_ind [0];

   int af_index = 0;
   

   phc_setup_error (1, "tools.source.php", 108, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 0;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 0; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE112 == NULL)
    {
      local_TLE112 = EG (uninitialized_zval_ptr);
      local_TLE112->refcount++;
    }
  zval** p_lhs = &local_TLE112;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE113 = 8;
{
        if (local_TLE113 == NULL)
    {
      local_TLE113 = EG (uninitialized_zval_ptr);
      local_TLE113->refcount++;
    }
  zval** p_lhs = &local_TLE113;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE114 = ($TLE112 << $TLE113);
{
    if (local_TLE114 == NULL)
    {
      local_TLE114 = EG (uninitialized_zval_ptr);
      local_TLE114->refcount++;
    }
  zval** p_lhs = &local_TLE114;

    zval* left;
  if (local_TLE112 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE112;

    zval* right;
  if (local_TLE113 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE113;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $int = ($int + $TLE114);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE114 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE114;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE115 = $this->readByte();
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "readByte", "tools.source.php", 109 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[0];
   int abr_index = 0;
   

   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [0];
   zval* args [0];
   zval** args_ind [0];

   int af_index = 0;
   

   phc_setup_error (1, "tools.source.php", 109, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 0;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 0; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE115 == NULL)
    {
      local_TLE115 = EG (uninitialized_zval_ptr);
      local_TLE115->refcount++;
    }
  zval** p_lhs = &local_TLE115;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $int = ($int + $TLE115);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE115 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE115;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE116 = (int) $int;
{
      if (local_TLE116 == NULL)
    {
      local_TLE116 = EG (uninitialized_zval_ptr);
      local_TLE116->refcount++;
    }
  zval** p_lhs = &local_TLE116;

    zval* rhs;
  if (local_int == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_int;

  if (*p_lhs != rhs)
    {
        if ((*p_lhs)->is_ref)
      overwrite_lhs (*p_lhs, rhs);
  else
    {
      zval_ptr_dtor (p_lhs);
        if (rhs->is_ref)
    {
      // Take a copy of RHS for LHS
      *p_lhs = zvp_clone_ex (rhs);
    }
  else
    {
      // Share a copy
      rhs->refcount++;
      *p_lhs = rhs;
    }

    }

    }

    assert (IS_LONG >= 0 && IS_LONG <= 6);
  if ((*p_lhs)->type != IS_LONG)
  {
    sep_copy_on_write (p_lhs);
    convert_to_long (*p_lhs);
  }

phc_check_invariants (TSRMLS_C);
}
// return $TLE116;
{
     zval* rhs;
  if (local_TLE116 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE116;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE107 != NULL)
{
zval_ptr_dtor (&local_TLE107);
}
if (local_TLE108 != NULL)
{
zval_ptr_dtor (&local_TLE108);
}
if (local_TLE109 != NULL)
{
zval_ptr_dtor (&local_TLE109);
}
if (local_TLE110 != NULL)
{
zval_ptr_dtor (&local_TLE110);
}
if (local_TLE111 != NULL)
{
zval_ptr_dtor (&local_TLE111);
}
if (local_TLE112 != NULL)
{
zval_ptr_dtor (&local_TLE112);
}
if (local_TLE113 != NULL)
{
zval_ptr_dtor (&local_TLE113);
}
if (local_TLE114 != NULL)
{
zval_ptr_dtor (&local_TLE114);
}
if (local_TLE115 != NULL)
{
zval_ptr_dtor (&local_TLE115);
}
if (local_TLE116 != NULL)
{
zval_ptr_dtor (&local_TLE116);
}
if (local_int != NULL)
{
zval_ptr_dtor (&local_int);
}
}
// public function resetReadPointer()
// {
// 	$TLE117 = 0;
// 	$this->_readPos = $TLE117;
// }
PHP_METHOD(Memory, resetReadPointer)
{
zval* local_TLE117 = NULL;
zval* local_this = getThis();
// Function body
// $TLE117 = 0;
{
        if (local_TLE117 == NULL)
    {
      local_TLE117 = EG (uninitialized_zval_ptr);
      local_TLE117->refcount++;
    }
  zval** p_lhs = &local_TLE117;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $this->_readPos = $TLE117;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE117 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE117;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_readPos", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE117 != NULL)
{
zval_ptr_dtor (&local_TLE117);
}
}
// public function setReadPointer($value)
// {
// 	$this->_readPos = $value;
// }
PHP_METHOD(Memory, setReadPointer)
{
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $this->_readPos = $value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_value == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_value;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_readPos", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function setByte($pos, $byte)
// {
// 	$TSt118 =& $this->_buffer;
// 	$TLE119 = 255;
// 	$TLE120 = ($byte & $TLE119);
// 	$TLE121 = chr($TLE120);
// 	$TSt118[$pos] = $TLE121;
// 	$TSt122 =& $this->_mem;
// 	$TLE123 = 255;
// 	$TLE124 = ($byte & $TLE123);
// 	$TSt122[$pos] = $TLE124;
// }
PHP_METHOD(Memory, setByte)
{
zval* local_TLE119 = NULL;
zval* local_TLE120 = NULL;
zval* local_TLE121 = NULL;
zval* local_TLE123 = NULL;
zval* local_TLE124 = NULL;
zval* local_TSt118 = NULL;
zval* local_TSt122 = NULL;
zval* local_byte = NULL;
zval* local_pos = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[2];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_pos != NULL)
{
	zval_ptr_dtor (&local_pos);
}
local_pos = params[0];
// param 1
params[1]->refcount++;
if (local_byte != NULL)
{
	zval_ptr_dtor (&local_byte);
}
local_byte = params[1];
}
// Function body
// $TSt118 =& $this->_buffer;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_buffer", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TSt118 == NULL)
    {
      local_TSt118 = EG (uninitialized_zval_ptr);
      local_TSt118->refcount++;
    }
  zval** p_lhs = &local_TSt118;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// $TLE119 = 255;
{
        if (local_TLE119 == NULL)
    {
      local_TLE119 = EG (uninitialized_zval_ptr);
      local_TLE119->refcount++;
    }
  zval** p_lhs = &local_TLE119;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE120 = ($byte & $TLE119);
{
    if (local_TLE120 == NULL)
    {
      local_TLE120 = EG (uninitialized_zval_ptr);
      local_TLE120->refcount++;
    }
  zval** p_lhs = &local_TLE120;

    zval* left;
  if (local_byte == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_byte;

    zval* right;
  if (local_TLE119 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE119;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE121 = chr($TLE120);
{
   initialize_function_call (&chr_fci, &chr_fcic, "chr", "tools.source.php", 120 TSRMLS_CC);
      zend_function* signature = chr_fcic.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE120 == NULL)
    {
      local_TLE120 = EG (uninitialized_zval_ptr);
      local_TLE120->refcount++;
    }
  zval** p_arg = &local_TLE120;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE120 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE120;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 120, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = chr_fci.param_count;
   zval*** params_save = chr_fci.params;
   zval** retval_save = chr_fci.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   chr_fci.params = args_ind;
   chr_fci.param_count = 1;
   chr_fci.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&chr_fci, &chr_fcic TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   chr_fci.params = params_save;
   chr_fci.param_count = param_count_save;
   chr_fci.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE121 == NULL)
    {
      local_TLE121 = EG (uninitialized_zval_ptr);
      local_TLE121->refcount++;
    }
  zval** p_lhs = &local_TLE121;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TSt118[$pos] = $TLE121;
{
     if (local_TSt118 == NULL)
    {
      local_TSt118 = EG (uninitialized_zval_ptr);
      local_TSt118->refcount++;
    }
  zval** p_array = &local_TSt118;

   check_array_type (p_array TSRMLS_CC);

     zval* index;
  if (local_pos == NULL)
    index = EG (uninitialized_zval_ptr);
  else
    index = local_pos;


   // String indexing
   if (Z_TYPE_PP (p_array) == IS_STRING && Z_STRLEN_PP (p_array) > 0)
   {
        zval* rhs;
  if (local_TLE121 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE121;

      write_string_index (p_array, index, rhs TSRMLS_CC);
   }
   else if (Z_TYPE_PP (p_array) == IS_ARRAY)
   {
      zval** p_lhs = get_ht_entry (p_array, index TSRMLS_CC);
        zval* rhs;
  if (local_TLE121 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE121;

      if (*p_lhs != rhs)
      {
	 write_var (p_lhs, rhs);
      }
   }
phc_check_invariants (TSRMLS_C);
}
// $TSt122 =& $this->_mem;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_mem", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TSt122 == NULL)
    {
      local_TSt122 = EG (uninitialized_zval_ptr);
      local_TSt122->refcount++;
    }
  zval** p_lhs = &local_TSt122;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// $TLE123 = 255;
{
        if (local_TLE123 == NULL)
    {
      local_TLE123 = EG (uninitialized_zval_ptr);
      local_TLE123->refcount++;
    }
  zval** p_lhs = &local_TLE123;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE124 = ($byte & $TLE123);
{
    if (local_TLE124 == NULL)
    {
      local_TLE124 = EG (uninitialized_zval_ptr);
      local_TLE124->refcount++;
    }
  zval** p_lhs = &local_TLE124;

    zval* left;
  if (local_byte == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_byte;

    zval* right;
  if (local_TLE123 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE123;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TSt122[$pos] = $TLE124;
{
     if (local_TSt122 == NULL)
    {
      local_TSt122 = EG (uninitialized_zval_ptr);
      local_TSt122->refcount++;
    }
  zval** p_array = &local_TSt122;

   check_array_type (p_array TSRMLS_CC);

     zval* index;
  if (local_pos == NULL)
    index = EG (uninitialized_zval_ptr);
  else
    index = local_pos;


   // String indexing
   if (Z_TYPE_PP (p_array) == IS_STRING && Z_STRLEN_PP (p_array) > 0)
   {
        zval* rhs;
  if (local_TLE124 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE124;

      write_string_index (p_array, index, rhs TSRMLS_CC);
   }
   else if (Z_TYPE_PP (p_array) == IS_ARRAY)
   {
      zval** p_lhs = get_ht_entry (p_array, index TSRMLS_CC);
        zval* rhs;
  if (local_TLE124 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE124;

      if (*p_lhs != rhs)
      {
	 write_var (p_lhs, rhs);
      }
   }
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE119 != NULL)
{
zval_ptr_dtor (&local_TLE119);
}
if (local_TLE120 != NULL)
{
zval_ptr_dtor (&local_TLE120);
}
if (local_TLE121 != NULL)
{
zval_ptr_dtor (&local_TLE121);
}
if (local_TLE123 != NULL)
{
zval_ptr_dtor (&local_TLE123);
}
if (local_TLE124 != NULL)
{
zval_ptr_dtor (&local_TLE124);
}
if (local_TSt118 != NULL)
{
zval_ptr_dtor (&local_TSt118);
}
if (local_TSt122 != NULL)
{
zval_ptr_dtor (&local_TSt122);
}
if (local_byte != NULL)
{
zval_ptr_dtor (&local_byte);
}
if (local_pos != NULL)
{
zval_ptr_dtor (&local_pos);
}
}
// public function setShort($pos, $short)
// {
// 	$TLE125 = 65535;
// 	$short = ($short & $TLE125);
// 	$TLE126 = 8;
// 	$TLE127 = ($short >> $TLE126);
// 	$this->setByte($pos, $TLE127);
// 	$TLE128 = 8;
// 	$TLE129 = ($short >> $TLE128);
// 	$TLE130 = 8;
// 	$TLE131 = ($TLE129 << $TLE130);
// 	$short = ($short - $TLE131);
// 	$TLE132 = 1;
// 	$TLE133 = ($pos + $TLE132);
// 	$this->setByte($TLE133, $short);
// }
PHP_METHOD(Memory, setShort)
{
zval* local_TLE125 = NULL;
zval* local_TLE126 = NULL;
zval* local_TLE127 = NULL;
zval* local_TLE128 = NULL;
zval* local_TLE129 = NULL;
zval* local_TLE130 = NULL;
zval* local_TLE131 = NULL;
zval* local_TLE132 = NULL;
zval* local_TLE133 = NULL;
zval* local_pos = NULL;
zval* local_short = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[2];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_pos != NULL)
{
	zval_ptr_dtor (&local_pos);
}
local_pos = params[0];
// param 1
params[1]->refcount++;
if (local_short != NULL)
{
	zval_ptr_dtor (&local_short);
}
local_short = params[1];
}
// Function body
// $TLE125 = 65535;
{
        if (local_TLE125 == NULL)
    {
      local_TLE125 = EG (uninitialized_zval_ptr);
      local_TLE125->refcount++;
    }
  zval** p_lhs = &local_TLE125;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65535);

phc_check_invariants (TSRMLS_C);
}
// $short = ($short & $TLE125);
{
    if (local_short == NULL)
    {
      local_short = EG (uninitialized_zval_ptr);
      local_short->refcount++;
    }
  zval** p_lhs = &local_short;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE125 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE125;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE126 = 8;
{
        if (local_TLE126 == NULL)
    {
      local_TLE126 = EG (uninitialized_zval_ptr);
      local_TLE126->refcount++;
    }
  zval** p_lhs = &local_TLE126;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE127 = ($short >> $TLE126);
{
    if (local_TLE127 == NULL)
    {
      local_TLE127 = EG (uninitialized_zval_ptr);
      local_TLE127->refcount++;
    }
  zval** p_lhs = &local_TLE127;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE126 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE126;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->setByte($pos, $TLE127);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "setByte", "tools.source.php", 125 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[2];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;
   // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [2];
   zval* args [2];
   zval** args_ind [2];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_pos == NULL)
    {
      local_pos = EG (uninitialized_zval_ptr);
      local_pos->refcount++;
    }
  zval** p_arg = &local_pos;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_pos == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_pos;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;
   destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE127 == NULL)
    {
      local_TLE127 = EG (uninitialized_zval_ptr);
      local_TLE127->refcount++;
    }
  zval** p_arg = &local_TLE127;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE127 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE127;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 125, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 2;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 2; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE128 = 8;
{
        if (local_TLE128 == NULL)
    {
      local_TLE128 = EG (uninitialized_zval_ptr);
      local_TLE128->refcount++;
    }
  zval** p_lhs = &local_TLE128;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE129 = ($short >> $TLE128);
{
    if (local_TLE129 == NULL)
    {
      local_TLE129 = EG (uninitialized_zval_ptr);
      local_TLE129->refcount++;
    }
  zval** p_lhs = &local_TLE129;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE128 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE128;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE130 = 8;
{
        if (local_TLE130 == NULL)
    {
      local_TLE130 = EG (uninitialized_zval_ptr);
      local_TLE130->refcount++;
    }
  zval** p_lhs = &local_TLE130;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE131 = ($TLE129 << $TLE130);
{
    if (local_TLE131 == NULL)
    {
      local_TLE131 = EG (uninitialized_zval_ptr);
      local_TLE131->refcount++;
    }
  zval** p_lhs = &local_TLE131;

    zval* left;
  if (local_TLE129 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE129;

    zval* right;
  if (local_TLE130 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE130;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $short = ($short - $TLE131);
{
    if (local_short == NULL)
    {
      local_short = EG (uninitialized_zval_ptr);
      local_short->refcount++;
    }
  zval** p_lhs = &local_short;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE131 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE131;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE132 = 1;
{
        if (local_TLE132 == NULL)
    {
      local_TLE132 = EG (uninitialized_zval_ptr);
      local_TLE132->refcount++;
    }
  zval** p_lhs = &local_TLE132;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 1);

phc_check_invariants (TSRMLS_C);
}
// $TLE133 = ($pos + $TLE132);
{
    if (local_TLE133 == NULL)
    {
      local_TLE133 = EG (uninitialized_zval_ptr);
      local_TLE133->refcount++;
    }
  zval** p_lhs = &local_TLE133;

    zval* left;
  if (local_pos == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_pos;

    zval* right;
  if (local_TLE132 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE132;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->setByte($TLE133, $short);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "setByte", "tools.source.php", 127 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[2];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;
   // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [2];
   zval* args [2];
   zval** args_ind [2];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE133 == NULL)
    {
      local_TLE133 = EG (uninitialized_zval_ptr);
      local_TLE133->refcount++;
    }
  zval** p_arg = &local_TLE133;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE133 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE133;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;
   destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_short == NULL)
    {
      local_short = EG (uninitialized_zval_ptr);
      local_short->refcount++;
    }
  zval** p_arg = &local_short;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_short == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_short;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 127, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 2;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 2; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE125 != NULL)
{
zval_ptr_dtor (&local_TLE125);
}
if (local_TLE126 != NULL)
{
zval_ptr_dtor (&local_TLE126);
}
if (local_TLE127 != NULL)
{
zval_ptr_dtor (&local_TLE127);
}
if (local_TLE128 != NULL)
{
zval_ptr_dtor (&local_TLE128);
}
if (local_TLE129 != NULL)
{
zval_ptr_dtor (&local_TLE129);
}
if (local_TLE130 != NULL)
{
zval_ptr_dtor (&local_TLE130);
}
if (local_TLE131 != NULL)
{
zval_ptr_dtor (&local_TLE131);
}
if (local_TLE132 != NULL)
{
zval_ptr_dtor (&local_TLE132);
}
if (local_TLE133 != NULL)
{
zval_ptr_dtor (&local_TLE133);
}
if (local_pos != NULL)
{
zval_ptr_dtor (&local_pos);
}
if (local_short != NULL)
{
zval_ptr_dtor (&local_short);
}
}
// public function setInteger($pos, $int)
// {
// 	$TLE134 = 4294967295.0;
// 	$int = ($int & $TLE134);
// 	$TLE135 = 24;
// 	$TLE136 = ($int >> $TLE135);
// 	$TLE137 = 255;
// 	$TLE138 = ($TLE136 & $TLE137);
// 	$this->setByte($pos, $TLE138);
// 	$TLE139 = 24;
// 	$TLE140 = ($int >> $TLE139);
// 	$TLE141 = 255;
// 	$TLE142 = ($TLE140 & $TLE141);
// 	$TLE143 = 24;
// 	$TLE144 = ($TLE142 << $TLE143);
// 	$int = ($int - $TLE144);
// 	$TLE145 = 1;
// 	$TLE146 = ($pos + $TLE145);
// 	$TLE147 = 16;
// 	$TLE148 = ($int >> $TLE147);
// 	$TLE149 = 255;
// 	$TLE150 = ($TLE148 & $TLE149);
// 	$this->setByte($TLE146, $TLE150);
// 	$TLE151 = 16;
// 	$TLE152 = ($int >> $TLE151);
// 	$TLE153 = 255;
// 	$TLE154 = ($TLE152 & $TLE153);
// 	$TLE155 = 16;
// 	$TLE156 = ($TLE154 << $TLE155);
// 	$int = ($int - $TLE156);
// 	$TLE157 = 2;
// 	$TLE158 = ($pos + $TLE157);
// 	$TLE159 = 8;
// 	$TLE160 = ($int >> $TLE159);
// 	$TLE161 = 255;
// 	$TLE162 = ($TLE160 & $TLE161);
// 	$this->setByte($TLE158, $TLE162);
// 	$TLE163 = 8;
// 	$TLE164 = ($int >> $TLE163);
// 	$TLE165 = 255;
// 	$TLE166 = ($TLE164 & $TLE165);
// 	$TLE167 = 8;
// 	$TLE168 = ($TLE166 << $TLE167);
// 	$int = ($int - $TLE168);
// 	$TLE169 = 3;
// 	$TLE170 = ($pos + $TLE169);
// 	$this->setByte($TLE170, $int);
// }
PHP_METHOD(Memory, setInteger)
{
zval* local_TLE134 = NULL;
zval* local_TLE135 = NULL;
zval* local_TLE136 = NULL;
zval* local_TLE137 = NULL;
zval* local_TLE138 = NULL;
zval* local_TLE139 = NULL;
zval* local_TLE140 = NULL;
zval* local_TLE141 = NULL;
zval* local_TLE142 = NULL;
zval* local_TLE143 = NULL;
zval* local_TLE144 = NULL;
zval* local_TLE145 = NULL;
zval* local_TLE146 = NULL;
zval* local_TLE147 = NULL;
zval* local_TLE148 = NULL;
zval* local_TLE149 = NULL;
zval* local_TLE150 = NULL;
zval* local_TLE151 = NULL;
zval* local_TLE152 = NULL;
zval* local_TLE153 = NULL;
zval* local_TLE154 = NULL;
zval* local_TLE155 = NULL;
zval* local_TLE156 = NULL;
zval* local_TLE157 = NULL;
zval* local_TLE158 = NULL;
zval* local_TLE159 = NULL;
zval* local_TLE160 = NULL;
zval* local_TLE161 = NULL;
zval* local_TLE162 = NULL;
zval* local_TLE163 = NULL;
zval* local_TLE164 = NULL;
zval* local_TLE165 = NULL;
zval* local_TLE166 = NULL;
zval* local_TLE167 = NULL;
zval* local_TLE168 = NULL;
zval* local_TLE169 = NULL;
zval* local_TLE170 = NULL;
zval* local_int = NULL;
zval* local_pos = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[2];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_pos != NULL)
{
	zval_ptr_dtor (&local_pos);
}
local_pos = params[0];
// param 1
params[1]->refcount++;
if (local_int != NULL)
{
	zval_ptr_dtor (&local_int);
}
local_int = params[1];
}
// Function body
// $TLE134 = 4294967295.0;
{
        if (local_TLE134 == NULL)
    {
      local_TLE134 = EG (uninitialized_zval_ptr);
      local_TLE134->refcount++;
    }
  zval** p_lhs = &local_TLE134;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   {
	unsigned char val[] = {
0, 0, 224, 255, 255, 255, 239, 65, };
ZVAL_DOUBLE (value, *(double*)(val));
}

phc_check_invariants (TSRMLS_C);
}
// $int = ($int & $TLE134);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE134 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE134;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE135 = 24;
{
        if (local_TLE135 == NULL)
    {
      local_TLE135 = EG (uninitialized_zval_ptr);
      local_TLE135->refcount++;
    }
  zval** p_lhs = &local_TLE135;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 24);

phc_check_invariants (TSRMLS_C);
}
// $TLE136 = ($int >> $TLE135);
{
    if (local_TLE136 == NULL)
    {
      local_TLE136 = EG (uninitialized_zval_ptr);
      local_TLE136->refcount++;
    }
  zval** p_lhs = &local_TLE136;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE135 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE135;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE137 = 255;
{
        if (local_TLE137 == NULL)
    {
      local_TLE137 = EG (uninitialized_zval_ptr);
      local_TLE137->refcount++;
    }
  zval** p_lhs = &local_TLE137;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE138 = ($TLE136 & $TLE137);
{
    if (local_TLE138 == NULL)
    {
      local_TLE138 = EG (uninitialized_zval_ptr);
      local_TLE138->refcount++;
    }
  zval** p_lhs = &local_TLE138;

    zval* left;
  if (local_TLE136 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE136;

    zval* right;
  if (local_TLE137 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE137;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->setByte($pos, $TLE138);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "setByte", "tools.source.php", 131 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[2];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;
   // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [2];
   zval* args [2];
   zval** args_ind [2];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_pos == NULL)
    {
      local_pos = EG (uninitialized_zval_ptr);
      local_pos->refcount++;
    }
  zval** p_arg = &local_pos;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_pos == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_pos;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;
   destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE138 == NULL)
    {
      local_TLE138 = EG (uninitialized_zval_ptr);
      local_TLE138->refcount++;
    }
  zval** p_arg = &local_TLE138;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE138 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE138;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 131, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 2;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 2; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE139 = 24;
{
        if (local_TLE139 == NULL)
    {
      local_TLE139 = EG (uninitialized_zval_ptr);
      local_TLE139->refcount++;
    }
  zval** p_lhs = &local_TLE139;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 24);

phc_check_invariants (TSRMLS_C);
}
// $TLE140 = ($int >> $TLE139);
{
    if (local_TLE140 == NULL)
    {
      local_TLE140 = EG (uninitialized_zval_ptr);
      local_TLE140->refcount++;
    }
  zval** p_lhs = &local_TLE140;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE139 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE139;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE141 = 255;
{
        if (local_TLE141 == NULL)
    {
      local_TLE141 = EG (uninitialized_zval_ptr);
      local_TLE141->refcount++;
    }
  zval** p_lhs = &local_TLE141;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE142 = ($TLE140 & $TLE141);
{
    if (local_TLE142 == NULL)
    {
      local_TLE142 = EG (uninitialized_zval_ptr);
      local_TLE142->refcount++;
    }
  zval** p_lhs = &local_TLE142;

    zval* left;
  if (local_TLE140 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE140;

    zval* right;
  if (local_TLE141 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE141;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE143 = 24;
{
        if (local_TLE143 == NULL)
    {
      local_TLE143 = EG (uninitialized_zval_ptr);
      local_TLE143->refcount++;
    }
  zval** p_lhs = &local_TLE143;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 24);

phc_check_invariants (TSRMLS_C);
}
// $TLE144 = ($TLE142 << $TLE143);
{
    if (local_TLE144 == NULL)
    {
      local_TLE144 = EG (uninitialized_zval_ptr);
      local_TLE144->refcount++;
    }
  zval** p_lhs = &local_TLE144;

    zval* left;
  if (local_TLE142 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE142;

    zval* right;
  if (local_TLE143 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE143;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $int = ($int - $TLE144);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE144 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE144;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE145 = 1;
{
        if (local_TLE145 == NULL)
    {
      local_TLE145 = EG (uninitialized_zval_ptr);
      local_TLE145->refcount++;
    }
  zval** p_lhs = &local_TLE145;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 1);

phc_check_invariants (TSRMLS_C);
}
// $TLE146 = ($pos + $TLE145);
{
    if (local_TLE146 == NULL)
    {
      local_TLE146 = EG (uninitialized_zval_ptr);
      local_TLE146->refcount++;
    }
  zval** p_lhs = &local_TLE146;

    zval* left;
  if (local_pos == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_pos;

    zval* right;
  if (local_TLE145 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE145;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE147 = 16;
{
        if (local_TLE147 == NULL)
    {
      local_TLE147 = EG (uninitialized_zval_ptr);
      local_TLE147->refcount++;
    }
  zval** p_lhs = &local_TLE147;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 16);

phc_check_invariants (TSRMLS_C);
}
// $TLE148 = ($int >> $TLE147);
{
    if (local_TLE148 == NULL)
    {
      local_TLE148 = EG (uninitialized_zval_ptr);
      local_TLE148->refcount++;
    }
  zval** p_lhs = &local_TLE148;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE147 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE147;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE149 = 255;
{
        if (local_TLE149 == NULL)
    {
      local_TLE149 = EG (uninitialized_zval_ptr);
      local_TLE149->refcount++;
    }
  zval** p_lhs = &local_TLE149;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE150 = ($TLE148 & $TLE149);
{
    if (local_TLE150 == NULL)
    {
      local_TLE150 = EG (uninitialized_zval_ptr);
      local_TLE150->refcount++;
    }
  zval** p_lhs = &local_TLE150;

    zval* left;
  if (local_TLE148 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE148;

    zval* right;
  if (local_TLE149 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE149;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->setByte($TLE146, $TLE150);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "setByte", "tools.source.php", 133 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[2];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;
   // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [2];
   zval* args [2];
   zval** args_ind [2];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE146 == NULL)
    {
      local_TLE146 = EG (uninitialized_zval_ptr);
      local_TLE146->refcount++;
    }
  zval** p_arg = &local_TLE146;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE146 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE146;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;
   destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE150 == NULL)
    {
      local_TLE150 = EG (uninitialized_zval_ptr);
      local_TLE150->refcount++;
    }
  zval** p_arg = &local_TLE150;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE150 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE150;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 133, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 2;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 2; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE151 = 16;
{
        if (local_TLE151 == NULL)
    {
      local_TLE151 = EG (uninitialized_zval_ptr);
      local_TLE151->refcount++;
    }
  zval** p_lhs = &local_TLE151;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 16);

phc_check_invariants (TSRMLS_C);
}
// $TLE152 = ($int >> $TLE151);
{
    if (local_TLE152 == NULL)
    {
      local_TLE152 = EG (uninitialized_zval_ptr);
      local_TLE152->refcount++;
    }
  zval** p_lhs = &local_TLE152;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE151 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE151;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE153 = 255;
{
        if (local_TLE153 == NULL)
    {
      local_TLE153 = EG (uninitialized_zval_ptr);
      local_TLE153->refcount++;
    }
  zval** p_lhs = &local_TLE153;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE154 = ($TLE152 & $TLE153);
{
    if (local_TLE154 == NULL)
    {
      local_TLE154 = EG (uninitialized_zval_ptr);
      local_TLE154->refcount++;
    }
  zval** p_lhs = &local_TLE154;

    zval* left;
  if (local_TLE152 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE152;

    zval* right;
  if (local_TLE153 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE153;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE155 = 16;
{
        if (local_TLE155 == NULL)
    {
      local_TLE155 = EG (uninitialized_zval_ptr);
      local_TLE155->refcount++;
    }
  zval** p_lhs = &local_TLE155;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 16);

phc_check_invariants (TSRMLS_C);
}
// $TLE156 = ($TLE154 << $TLE155);
{
    if (local_TLE156 == NULL)
    {
      local_TLE156 = EG (uninitialized_zval_ptr);
      local_TLE156->refcount++;
    }
  zval** p_lhs = &local_TLE156;

    zval* left;
  if (local_TLE154 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE154;

    zval* right;
  if (local_TLE155 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE155;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $int = ($int - $TLE156);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE156 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE156;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE157 = 2;
{
        if (local_TLE157 == NULL)
    {
      local_TLE157 = EG (uninitialized_zval_ptr);
      local_TLE157->refcount++;
    }
  zval** p_lhs = &local_TLE157;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 2);

phc_check_invariants (TSRMLS_C);
}
// $TLE158 = ($pos + $TLE157);
{
    if (local_TLE158 == NULL)
    {
      local_TLE158 = EG (uninitialized_zval_ptr);
      local_TLE158->refcount++;
    }
  zval** p_lhs = &local_TLE158;

    zval* left;
  if (local_pos == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_pos;

    zval* right;
  if (local_TLE157 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE157;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE159 = 8;
{
        if (local_TLE159 == NULL)
    {
      local_TLE159 = EG (uninitialized_zval_ptr);
      local_TLE159->refcount++;
    }
  zval** p_lhs = &local_TLE159;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE160 = ($int >> $TLE159);
{
    if (local_TLE160 == NULL)
    {
      local_TLE160 = EG (uninitialized_zval_ptr);
      local_TLE160->refcount++;
    }
  zval** p_lhs = &local_TLE160;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE159 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE159;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE161 = 255;
{
        if (local_TLE161 == NULL)
    {
      local_TLE161 = EG (uninitialized_zval_ptr);
      local_TLE161->refcount++;
    }
  zval** p_lhs = &local_TLE161;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE162 = ($TLE160 & $TLE161);
{
    if (local_TLE162 == NULL)
    {
      local_TLE162 = EG (uninitialized_zval_ptr);
      local_TLE162->refcount++;
    }
  zval** p_lhs = &local_TLE162;

    zval* left;
  if (local_TLE160 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE160;

    zval* right;
  if (local_TLE161 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE161;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->setByte($TLE158, $TLE162);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "setByte", "tools.source.php", 135 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[2];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;
   // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [2];
   zval* args [2];
   zval** args_ind [2];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE158 == NULL)
    {
      local_TLE158 = EG (uninitialized_zval_ptr);
      local_TLE158->refcount++;
    }
  zval** p_arg = &local_TLE158;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE158 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE158;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;
   destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE162 == NULL)
    {
      local_TLE162 = EG (uninitialized_zval_ptr);
      local_TLE162->refcount++;
    }
  zval** p_arg = &local_TLE162;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE162 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE162;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 135, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 2;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 2; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE163 = 8;
{
        if (local_TLE163 == NULL)
    {
      local_TLE163 = EG (uninitialized_zval_ptr);
      local_TLE163->refcount++;
    }
  zval** p_lhs = &local_TLE163;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE164 = ($int >> $TLE163);
{
    if (local_TLE164 == NULL)
    {
      local_TLE164 = EG (uninitialized_zval_ptr);
      local_TLE164->refcount++;
    }
  zval** p_lhs = &local_TLE164;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE163 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE163;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_right_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE165 = 255;
{
        if (local_TLE165 == NULL)
    {
      local_TLE165 = EG (uninitialized_zval_ptr);
      local_TLE165->refcount++;
    }
  zval** p_lhs = &local_TLE165;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE166 = ($TLE164 & $TLE165);
{
    if (local_TLE166 == NULL)
    {
      local_TLE166 = EG (uninitialized_zval_ptr);
      local_TLE166->refcount++;
    }
  zval** p_lhs = &local_TLE166;

    zval* left;
  if (local_TLE164 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE164;

    zval* right;
  if (local_TLE165 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE165;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE167 = 8;
{
        if (local_TLE167 == NULL)
    {
      local_TLE167 = EG (uninitialized_zval_ptr);
      local_TLE167->refcount++;
    }
  zval** p_lhs = &local_TLE167;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE168 = ($TLE166 << $TLE167);
{
    if (local_TLE168 == NULL)
    {
      local_TLE168 = EG (uninitialized_zval_ptr);
      local_TLE168->refcount++;
    }
  zval** p_lhs = &local_TLE168;

    zval* left;
  if (local_TLE166 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE166;

    zval* right;
  if (local_TLE167 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE167;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $int = ($int - $TLE168);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE168 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE168;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE169 = 3;
{
        if (local_TLE169 == NULL)
    {
      local_TLE169 = EG (uninitialized_zval_ptr);
      local_TLE169->refcount++;
    }
  zval** p_lhs = &local_TLE169;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 3);

phc_check_invariants (TSRMLS_C);
}
// $TLE170 = ($pos + $TLE169);
{
    if (local_TLE170 == NULL)
    {
      local_TLE170 = EG (uninitialized_zval_ptr);
      local_TLE170->refcount++;
    }
  zval** p_lhs = &local_TLE170;

    zval* left;
  if (local_pos == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_pos;

    zval* right;
  if (local_TLE169 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE169;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->setByte($TLE170, $int);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "setByte", "tools.source.php", 137 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[2];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;
   // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [2];
   zval* args [2];
   zval** args_ind [2];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE170 == NULL)
    {
      local_TLE170 = EG (uninitialized_zval_ptr);
      local_TLE170->refcount++;
    }
  zval** p_arg = &local_TLE170;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE170 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE170;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;
   destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_arg = &local_int;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_int == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_int;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 137, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 2;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 2; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE134 != NULL)
{
zval_ptr_dtor (&local_TLE134);
}
if (local_TLE135 != NULL)
{
zval_ptr_dtor (&local_TLE135);
}
if (local_TLE136 != NULL)
{
zval_ptr_dtor (&local_TLE136);
}
if (local_TLE137 != NULL)
{
zval_ptr_dtor (&local_TLE137);
}
if (local_TLE138 != NULL)
{
zval_ptr_dtor (&local_TLE138);
}
if (local_TLE139 != NULL)
{
zval_ptr_dtor (&local_TLE139);
}
if (local_TLE140 != NULL)
{
zval_ptr_dtor (&local_TLE140);
}
if (local_TLE141 != NULL)
{
zval_ptr_dtor (&local_TLE141);
}
if (local_TLE142 != NULL)
{
zval_ptr_dtor (&local_TLE142);
}
if (local_TLE143 != NULL)
{
zval_ptr_dtor (&local_TLE143);
}
if (local_TLE144 != NULL)
{
zval_ptr_dtor (&local_TLE144);
}
if (local_TLE145 != NULL)
{
zval_ptr_dtor (&local_TLE145);
}
if (local_TLE146 != NULL)
{
zval_ptr_dtor (&local_TLE146);
}
if (local_TLE147 != NULL)
{
zval_ptr_dtor (&local_TLE147);
}
if (local_TLE148 != NULL)
{
zval_ptr_dtor (&local_TLE148);
}
if (local_TLE149 != NULL)
{
zval_ptr_dtor (&local_TLE149);
}
if (local_TLE150 != NULL)
{
zval_ptr_dtor (&local_TLE150);
}
if (local_TLE151 != NULL)
{
zval_ptr_dtor (&local_TLE151);
}
if (local_TLE152 != NULL)
{
zval_ptr_dtor (&local_TLE152);
}
if (local_TLE153 != NULL)
{
zval_ptr_dtor (&local_TLE153);
}
if (local_TLE154 != NULL)
{
zval_ptr_dtor (&local_TLE154);
}
if (local_TLE155 != NULL)
{
zval_ptr_dtor (&local_TLE155);
}
if (local_TLE156 != NULL)
{
zval_ptr_dtor (&local_TLE156);
}
if (local_TLE157 != NULL)
{
zval_ptr_dtor (&local_TLE157);
}
if (local_TLE158 != NULL)
{
zval_ptr_dtor (&local_TLE158);
}
if (local_TLE159 != NULL)
{
zval_ptr_dtor (&local_TLE159);
}
if (local_TLE160 != NULL)
{
zval_ptr_dtor (&local_TLE160);
}
if (local_TLE161 != NULL)
{
zval_ptr_dtor (&local_TLE161);
}
if (local_TLE162 != NULL)
{
zval_ptr_dtor (&local_TLE162);
}
if (local_TLE163 != NULL)
{
zval_ptr_dtor (&local_TLE163);
}
if (local_TLE164 != NULL)
{
zval_ptr_dtor (&local_TLE164);
}
if (local_TLE165 != NULL)
{
zval_ptr_dtor (&local_TLE165);
}
if (local_TLE166 != NULL)
{
zval_ptr_dtor (&local_TLE166);
}
if (local_TLE167 != NULL)
{
zval_ptr_dtor (&local_TLE167);
}
if (local_TLE168 != NULL)
{
zval_ptr_dtor (&local_TLE168);
}
if (local_TLE169 != NULL)
{
zval_ptr_dtor (&local_TLE169);
}
if (local_TLE170 != NULL)
{
zval_ptr_dtor (&local_TLE170);
}
if (local_int != NULL)
{
zval_ptr_dtor (&local_int);
}
if (local_pos != NULL)
{
zval_ptr_dtor (&local_pos);
}
}
// public function getByte($pos)
// {
// 	$TSt171 = $this->_mem;
// 	$TSi172 = $TSt171[$pos];
// 	return $TSi172;
// }
PHP_METHOD(Memory, getByte)
{
zval* local_TSi172 = NULL;
zval* local_TSt171 = NULL;
zval* local_pos = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_pos != NULL)
{
	zval_ptr_dtor (&local_pos);
}
local_pos = params[0];
}
// Function body
// $TSt171 = $this->_mem;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_mem", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt171 == NULL)
    {
      local_TSt171 = EG (uninitialized_zval_ptr);
      local_TSt171->refcount++;
    }
  zval** p_lhs = &local_TSt171;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TSi172 = $TSt171[$pos];
{
     if (local_TSi172 == NULL)
    {
      local_TSi172 = EG (uninitialized_zval_ptr);
      local_TSi172->refcount++;
    }
  zval** p_lhs = &local_TSi172;

     zval* r_array;
  if (local_TSt171 == NULL)
    r_array = EG (uninitialized_zval_ptr);
  else
    r_array = local_TSt171;

     zval* r_index;
  if (local_pos == NULL)
    r_index = EG (uninitialized_zval_ptr);
  else
    r_index = local_pos;


   zval* rhs;
   int is_rhs_new = 0;
    if (Z_TYPE_P (r_array) != IS_ARRAY)
    {
      if (Z_TYPE_P (r_array) == IS_STRING)
	{
	  is_rhs_new = 1;
	  rhs = read_string_index (r_array, r_index TSRMLS_CC);
	}
      else
	// TODO: warning here?
	rhs = EG (uninitialized_zval_ptr);
    }
    else
    {
      if (check_array_index_type (r_index TSRMLS_CC))
	{
	  // Read array variable
	  read_array (&rhs, r_array, r_index TSRMLS_CC);
	}
      else
	rhs = *p_lhs; // HACK to fail  *p_lhs != rhs
    }

   if (*p_lhs != rhs)
      write_var (p_lhs, rhs);

   if (is_rhs_new) zval_ptr_dtor (&rhs);
phc_check_invariants (TSRMLS_C);
}
// return $TSi172;
{
     zval* rhs;
  if (local_TSi172 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TSi172;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TSi172 != NULL)
{
zval_ptr_dtor (&local_TSi172);
}
if (local_TSt171 != NULL)
{
zval_ptr_dtor (&local_TSt171);
}
if (local_pos != NULL)
{
zval_ptr_dtor (&local_pos);
}
}
// public function getShort($pos)
// {
// 	$TLE173 = $this->getByte($pos);
// 	$TLE174 = 8;
// 	$short = ($TLE173 << $TLE174);
// 	$TLE175 = 1;
// 	$TLE176 = ($pos + $TLE175);
// 	$TLE177 = $this->getByte($TLE176);
// 	$short = ($short + $TLE177);
// 	return $short;
// }
PHP_METHOD(Memory, getShort)
{
zval* local_TLE173 = NULL;
zval* local_TLE174 = NULL;
zval* local_TLE175 = NULL;
zval* local_TLE176 = NULL;
zval* local_TLE177 = NULL;
zval* local_pos = NULL;
zval* local_short = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_pos != NULL)
{
	zval_ptr_dtor (&local_pos);
}
local_pos = params[0];
}
// Function body
// $TLE173 = $this->getByte($pos);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "getByte", "tools.source.php", 143 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_pos == NULL)
    {
      local_pos = EG (uninitialized_zval_ptr);
      local_pos->refcount++;
    }
  zval** p_arg = &local_pos;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_pos == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_pos;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 143, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE173 == NULL)
    {
      local_TLE173 = EG (uninitialized_zval_ptr);
      local_TLE173->refcount++;
    }
  zval** p_lhs = &local_TLE173;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE174 = 8;
{
        if (local_TLE174 == NULL)
    {
      local_TLE174 = EG (uninitialized_zval_ptr);
      local_TLE174->refcount++;
    }
  zval** p_lhs = &local_TLE174;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $short = ($TLE173 << $TLE174);
{
    if (local_short == NULL)
    {
      local_short = EG (uninitialized_zval_ptr);
      local_short->refcount++;
    }
  zval** p_lhs = &local_short;

    zval* left;
  if (local_TLE173 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE173;

    zval* right;
  if (local_TLE174 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE174;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE175 = 1;
{
        if (local_TLE175 == NULL)
    {
      local_TLE175 = EG (uninitialized_zval_ptr);
      local_TLE175->refcount++;
    }
  zval** p_lhs = &local_TLE175;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 1);

phc_check_invariants (TSRMLS_C);
}
// $TLE176 = ($pos + $TLE175);
{
    if (local_TLE176 == NULL)
    {
      local_TLE176 = EG (uninitialized_zval_ptr);
      local_TLE176->refcount++;
    }
  zval** p_lhs = &local_TLE176;

    zval* left;
  if (local_pos == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_pos;

    zval* right;
  if (local_TLE175 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE175;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE177 = $this->getByte($TLE176);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "getByte", "tools.source.php", 144 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE176 == NULL)
    {
      local_TLE176 = EG (uninitialized_zval_ptr);
      local_TLE176->refcount++;
    }
  zval** p_arg = &local_TLE176;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE176 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE176;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 144, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE177 == NULL)
    {
      local_TLE177 = EG (uninitialized_zval_ptr);
      local_TLE177->refcount++;
    }
  zval** p_lhs = &local_TLE177;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $short = ($short + $TLE177);
{
    if (local_short == NULL)
    {
      local_short = EG (uninitialized_zval_ptr);
      local_short->refcount++;
    }
  zval** p_lhs = &local_short;

    zval* left;
  if (local_short == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_short;

    zval* right;
  if (local_TLE177 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE177;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// return $short;
{
     zval* rhs;
  if (local_short == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_short;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE173 != NULL)
{
zval_ptr_dtor (&local_TLE173);
}
if (local_TLE174 != NULL)
{
zval_ptr_dtor (&local_TLE174);
}
if (local_TLE175 != NULL)
{
zval_ptr_dtor (&local_TLE175);
}
if (local_TLE176 != NULL)
{
zval_ptr_dtor (&local_TLE176);
}
if (local_TLE177 != NULL)
{
zval_ptr_dtor (&local_TLE177);
}
if (local_pos != NULL)
{
zval_ptr_dtor (&local_pos);
}
if (local_short != NULL)
{
zval_ptr_dtor (&local_short);
}
}
// public function getInteger($pos)
// {
// 	$TLE178 = $this->getByte($pos);
// 	$TLE179 = 24;
// 	$int = ($TLE178 << $TLE179);
// 	$TLE180 = 1;
// 	$TLE181 = ($pos + $TLE180);
// 	$TLE182 = $this->getByte($TLE181);
// 	$TLE183 = 16;
// 	$TLE184 = ($TLE182 << $TLE183);
// 	$int = ($int + $TLE184);
// 	$TLE185 = 2;
// 	$TLE186 = ($pos + $TLE185);
// 	$TLE187 = $this->getByte($TLE186);
// 	$TLE188 = 8;
// 	$TLE189 = ($TLE187 << $TLE188);
// 	$int = ($int + $TLE189);
// 	$TLE190 = 3;
// 	$TLE191 = ($pos + $TLE190);
// 	$TLE192 = $this->getByte($TLE191);
// 	$int = ($int + $TLE192);
// 	$TLE193 = (int) $int;
// 	return $TLE193;
// }
PHP_METHOD(Memory, getInteger)
{
zval* local_TLE178 = NULL;
zval* local_TLE179 = NULL;
zval* local_TLE180 = NULL;
zval* local_TLE181 = NULL;
zval* local_TLE182 = NULL;
zval* local_TLE183 = NULL;
zval* local_TLE184 = NULL;
zval* local_TLE185 = NULL;
zval* local_TLE186 = NULL;
zval* local_TLE187 = NULL;
zval* local_TLE188 = NULL;
zval* local_TLE189 = NULL;
zval* local_TLE190 = NULL;
zval* local_TLE191 = NULL;
zval* local_TLE192 = NULL;
zval* local_TLE193 = NULL;
zval* local_int = NULL;
zval* local_pos = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_pos != NULL)
{
	zval_ptr_dtor (&local_pos);
}
local_pos = params[0];
}
// Function body
// $TLE178 = $this->getByte($pos);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "getByte", "tools.source.php", 148 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_pos == NULL)
    {
      local_pos = EG (uninitialized_zval_ptr);
      local_pos->refcount++;
    }
  zval** p_arg = &local_pos;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_pos == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_pos;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 148, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE178 == NULL)
    {
      local_TLE178 = EG (uninitialized_zval_ptr);
      local_TLE178->refcount++;
    }
  zval** p_lhs = &local_TLE178;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE179 = 24;
{
        if (local_TLE179 == NULL)
    {
      local_TLE179 = EG (uninitialized_zval_ptr);
      local_TLE179->refcount++;
    }
  zval** p_lhs = &local_TLE179;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 24);

phc_check_invariants (TSRMLS_C);
}
// $int = ($TLE178 << $TLE179);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_TLE178 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE178;

    zval* right;
  if (local_TLE179 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE179;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE180 = 1;
{
        if (local_TLE180 == NULL)
    {
      local_TLE180 = EG (uninitialized_zval_ptr);
      local_TLE180->refcount++;
    }
  zval** p_lhs = &local_TLE180;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 1);

phc_check_invariants (TSRMLS_C);
}
// $TLE181 = ($pos + $TLE180);
{
    if (local_TLE181 == NULL)
    {
      local_TLE181 = EG (uninitialized_zval_ptr);
      local_TLE181->refcount++;
    }
  zval** p_lhs = &local_TLE181;

    zval* left;
  if (local_pos == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_pos;

    zval* right;
  if (local_TLE180 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE180;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE182 = $this->getByte($TLE181);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "getByte", "tools.source.php", 149 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE181 == NULL)
    {
      local_TLE181 = EG (uninitialized_zval_ptr);
      local_TLE181->refcount++;
    }
  zval** p_arg = &local_TLE181;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE181 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE181;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 149, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE182 == NULL)
    {
      local_TLE182 = EG (uninitialized_zval_ptr);
      local_TLE182->refcount++;
    }
  zval** p_lhs = &local_TLE182;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE183 = 16;
{
        if (local_TLE183 == NULL)
    {
      local_TLE183 = EG (uninitialized_zval_ptr);
      local_TLE183->refcount++;
    }
  zval** p_lhs = &local_TLE183;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 16);

phc_check_invariants (TSRMLS_C);
}
// $TLE184 = ($TLE182 << $TLE183);
{
    if (local_TLE184 == NULL)
    {
      local_TLE184 = EG (uninitialized_zval_ptr);
      local_TLE184->refcount++;
    }
  zval** p_lhs = &local_TLE184;

    zval* left;
  if (local_TLE182 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE182;

    zval* right;
  if (local_TLE183 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE183;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $int = ($int + $TLE184);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE184 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE184;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE185 = 2;
{
        if (local_TLE185 == NULL)
    {
      local_TLE185 = EG (uninitialized_zval_ptr);
      local_TLE185->refcount++;
    }
  zval** p_lhs = &local_TLE185;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 2);

phc_check_invariants (TSRMLS_C);
}
// $TLE186 = ($pos + $TLE185);
{
    if (local_TLE186 == NULL)
    {
      local_TLE186 = EG (uninitialized_zval_ptr);
      local_TLE186->refcount++;
    }
  zval** p_lhs = &local_TLE186;

    zval* left;
  if (local_pos == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_pos;

    zval* right;
  if (local_TLE185 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE185;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE187 = $this->getByte($TLE186);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "getByte", "tools.source.php", 150 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE186 == NULL)
    {
      local_TLE186 = EG (uninitialized_zval_ptr);
      local_TLE186->refcount++;
    }
  zval** p_arg = &local_TLE186;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE186 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE186;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 150, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE187 == NULL)
    {
      local_TLE187 = EG (uninitialized_zval_ptr);
      local_TLE187->refcount++;
    }
  zval** p_lhs = &local_TLE187;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE188 = 8;
{
        if (local_TLE188 == NULL)
    {
      local_TLE188 = EG (uninitialized_zval_ptr);
      local_TLE188->refcount++;
    }
  zval** p_lhs = &local_TLE188;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 8);

phc_check_invariants (TSRMLS_C);
}
// $TLE189 = ($TLE187 << $TLE188);
{
    if (local_TLE189 == NULL)
    {
      local_TLE189 = EG (uninitialized_zval_ptr);
      local_TLE189->refcount++;
    }
  zval** p_lhs = &local_TLE189;

    zval* left;
  if (local_TLE187 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE187;

    zval* right;
  if (local_TLE188 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE188;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  shift_left_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $int = ($int + $TLE189);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE189 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE189;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE190 = 3;
{
        if (local_TLE190 == NULL)
    {
      local_TLE190 = EG (uninitialized_zval_ptr);
      local_TLE190->refcount++;
    }
  zval** p_lhs = &local_TLE190;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 3);

phc_check_invariants (TSRMLS_C);
}
// $TLE191 = ($pos + $TLE190);
{
    if (local_TLE191 == NULL)
    {
      local_TLE191 = EG (uninitialized_zval_ptr);
      local_TLE191->refcount++;
    }
  zval** p_lhs = &local_TLE191;

    zval* left;
  if (local_pos == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_pos;

    zval* right;
  if (local_TLE190 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE190;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE192 = $this->getByte($TLE191);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "getByte", "tools.source.php", 151 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE191 == NULL)
    {
      local_TLE191 = EG (uninitialized_zval_ptr);
      local_TLE191->refcount++;
    }
  zval** p_arg = &local_TLE191;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE191 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE191;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 151, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE192 == NULL)
    {
      local_TLE192 = EG (uninitialized_zval_ptr);
      local_TLE192->refcount++;
    }
  zval** p_lhs = &local_TLE192;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $int = ($int + $TLE192);
{
    if (local_int == NULL)
    {
      local_int = EG (uninitialized_zval_ptr);
      local_int->refcount++;
    }
  zval** p_lhs = &local_int;

    zval* left;
  if (local_int == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_int;

    zval* right;
  if (local_TLE192 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE192;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE193 = (int) $int;
{
      if (local_TLE193 == NULL)
    {
      local_TLE193 = EG (uninitialized_zval_ptr);
      local_TLE193->refcount++;
    }
  zval** p_lhs = &local_TLE193;

    zval* rhs;
  if (local_int == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_int;

  if (*p_lhs != rhs)
    {
        if ((*p_lhs)->is_ref)
      overwrite_lhs (*p_lhs, rhs);
  else
    {
      zval_ptr_dtor (p_lhs);
        if (rhs->is_ref)
    {
      // Take a copy of RHS for LHS
      *p_lhs = zvp_clone_ex (rhs);
    }
  else
    {
      // Share a copy
      rhs->refcount++;
      *p_lhs = rhs;
    }

    }

    }

    assert (IS_LONG >= 0 && IS_LONG <= 6);
  if ((*p_lhs)->type != IS_LONG)
  {
    sep_copy_on_write (p_lhs);
    convert_to_long (*p_lhs);
  }

phc_check_invariants (TSRMLS_C);
}
// return $TLE193;
{
     zval* rhs;
  if (local_TLE193 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE193;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE178 != NULL)
{
zval_ptr_dtor (&local_TLE178);
}
if (local_TLE179 != NULL)
{
zval_ptr_dtor (&local_TLE179);
}
if (local_TLE180 != NULL)
{
zval_ptr_dtor (&local_TLE180);
}
if (local_TLE181 != NULL)
{
zval_ptr_dtor (&local_TLE181);
}
if (local_TLE182 != NULL)
{
zval_ptr_dtor (&local_TLE182);
}
if (local_TLE183 != NULL)
{
zval_ptr_dtor (&local_TLE183);
}
if (local_TLE184 != NULL)
{
zval_ptr_dtor (&local_TLE184);
}
if (local_TLE185 != NULL)
{
zval_ptr_dtor (&local_TLE185);
}
if (local_TLE186 != NULL)
{
zval_ptr_dtor (&local_TLE186);
}
if (local_TLE187 != NULL)
{
zval_ptr_dtor (&local_TLE187);
}
if (local_TLE188 != NULL)
{
zval_ptr_dtor (&local_TLE188);
}
if (local_TLE189 != NULL)
{
zval_ptr_dtor (&local_TLE189);
}
if (local_TLE190 != NULL)
{
zval_ptr_dtor (&local_TLE190);
}
if (local_TLE191 != NULL)
{
zval_ptr_dtor (&local_TLE191);
}
if (local_TLE192 != NULL)
{
zval_ptr_dtor (&local_TLE192);
}
if (local_TLE193 != NULL)
{
zval_ptr_dtor (&local_TLE193);
}
if (local_int != NULL)
{
zval_ptr_dtor (&local_int);
}
if (local_pos != NULL)
{
zval_ptr_dtor (&local_pos);
}
}
// public function getMemory($startPos = 0, $endPos = -1)
// {
// 	$TLE194 = 0;
// 	$TLE3 = ($startPos == $TLE194);
// 	if (TLE3) goto L334 else goto L335;
// L334:
// 	$TLE195 = -1;
// 	$TEF4 = ($endPos == $TLE195);
// 	goto L336;
// L335:
// 	$TEF4 = $TLE3;
// 	goto L336;
// L336:
// 	$TLE196 = (bool) $TEF4;
// 	if (TLE196) goto L343 else goto L344;
// L343:
// 	$TSt197 = $this->_buffer;
// 	return $TSt197;
// 	goto L345;
// L344:
// 	$TLE198 = -1;
// 	$TLE199 = ($endPos == $TLE198);
// 	if (TLE199) goto L337 else goto L338;
// L337:
// 	$TSt200 = $this->_pos;
// 	$endPos = $TSt200;
// 	goto L339;
// L338:
// 	goto L339;
// L339:
// 	$TLE317 = param_is_ref (NULL, "substr", 0);
// 	if (TLE317) goto L340 else goto L341;
// L340:
// 	$TMIt316 =& $this->_buffer;
// 	goto L342;
// L341:
// 	$TMIt316 = $this->_buffer;
// 	goto L342;
// L342:
// 	$TLE201 = substr($TMIt316, $startPos, $endPos);
// 	return $TLE201;
// 	goto L345;
// L345:
// }
PHP_METHOD(Memory, getMemory)
{
zval* local_TEF4 = NULL;
zval* local_TLE194 = NULL;
zval* local_TLE195 = NULL;
zval* local_TLE196 = NULL;
zval* local_TLE198 = NULL;
zval* local_TLE199 = NULL;
zval* local_TLE201 = NULL;
zval* local_TLE3 = NULL;
zval* local_TLE317 = NULL;
zval* local_TMIt316 = NULL;
zval* local_TSt197 = NULL;
zval* local_TSt200 = NULL;
zval* local_endPos = NULL;
zval* local_startPos = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[2];
zend_get_parameters_array(0, num_args, params);
// param 0
if (num_args <= 0)
{
zval* default_value;
{
zval* local___static_value__ = NULL;
// $__static_value__ = 0;
{
        if (local___static_value__ == NULL)
    {
      local___static_value__ = EG (uninitialized_zval_ptr);
      local___static_value__->refcount++;
    }
  zval** p_lhs = &local___static_value__;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
default_value = local___static_value__;
assert(!default_value->is_ref);
default_value->refcount++;
if (local___static_value__ != NULL)
{
zval_ptr_dtor (&local___static_value__);
}
}
default_value->refcount--;
	params[0] = default_value;
}
params[0]->refcount++;
if (local_startPos != NULL)
{
	zval_ptr_dtor (&local_startPos);
}
local_startPos = params[0];
// param 1
if (num_args <= 1)
{
zval* default_value;
{
zval* local___static_value__ = NULL;
// $__static_value__ = -1;
{
        if (local___static_value__ == NULL)
    {
      local___static_value__ = EG (uninitialized_zval_ptr);
      local___static_value__->refcount++;
    }
  zval** p_lhs = &local___static_value__;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, -1);

phc_check_invariants (TSRMLS_C);
}
default_value = local___static_value__;
assert(!default_value->is_ref);
default_value->refcount++;
if (local___static_value__ != NULL)
{
zval_ptr_dtor (&local___static_value__);
}
}
default_value->refcount--;
	params[1] = default_value;
}
params[1]->refcount++;
if (local_endPos != NULL)
{
	zval_ptr_dtor (&local_endPos);
}
local_endPos = params[1];
}
// Function body
// $TLE194 = 0;
{
        if (local_TLE194 == NULL)
    {
      local_TLE194 = EG (uninitialized_zval_ptr);
      local_TLE194->refcount++;
    }
  zval** p_lhs = &local_TLE194;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $TLE3 = ($startPos == $TLE194);
{
    if (local_TLE3 == NULL)
    {
      local_TLE3 = EG (uninitialized_zval_ptr);
      local_TLE3->refcount++;
    }
  zval** p_lhs = &local_TLE3;

    zval* left;
  if (local_startPos == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_startPos;

    zval* right;
  if (local_TLE194 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE194;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_equal_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE3) goto L334 else goto L335;
{
     zval* p_cond;
  if (local_TLE3 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE3;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L334;
   else
      goto L335;
phc_check_invariants (TSRMLS_C);
}
// L334:
L334:;
// $TLE195 = -1;
{
        if (local_TLE195 == NULL)
    {
      local_TLE195 = EG (uninitialized_zval_ptr);
      local_TLE195->refcount++;
    }
  zval** p_lhs = &local_TLE195;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, -1);

phc_check_invariants (TSRMLS_C);
}
// $TEF4 = ($endPos == $TLE195);
{
    if (local_TEF4 == NULL)
    {
      local_TEF4 = EG (uninitialized_zval_ptr);
      local_TEF4->refcount++;
    }
  zval** p_lhs = &local_TEF4;

    zval* left;
  if (local_endPos == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_endPos;

    zval* right;
  if (local_TLE195 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE195;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_equal_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// goto L336;
{
goto L336;
phc_check_invariants (TSRMLS_C);
}
// L335:
L335:;
// $TEF4 = $TLE3;
{
    if (local_TEF4 == NULL)
    {
      local_TEF4 = EG (uninitialized_zval_ptr);
      local_TEF4->refcount++;
    }
  zval** p_lhs = &local_TEF4;

    zval* rhs;
  if (local_TLE3 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE3;

  if (*p_lhs != rhs)
    {
        if ((*p_lhs)->is_ref)
      overwrite_lhs (*p_lhs, rhs);
  else
    {
      zval_ptr_dtor (p_lhs);
        if (rhs->is_ref)
    {
      // Take a copy of RHS for LHS
      *p_lhs = zvp_clone_ex (rhs);
    }
  else
    {
      // Share a copy
      rhs->refcount++;
      *p_lhs = rhs;
    }

    }

    }
phc_check_invariants (TSRMLS_C);
}
// goto L336;
{
goto L336;
phc_check_invariants (TSRMLS_C);
}
// L336:
L336:;
// $TLE196 = (bool) $TEF4;
{
      if (local_TLE196 == NULL)
    {
      local_TLE196 = EG (uninitialized_zval_ptr);
      local_TLE196->refcount++;
    }
  zval** p_lhs = &local_TLE196;

    zval* rhs;
  if (local_TEF4 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TEF4;

  if (*p_lhs != rhs)
    {
        if ((*p_lhs)->is_ref)
      overwrite_lhs (*p_lhs, rhs);
  else
    {
      zval_ptr_dtor (p_lhs);
        if (rhs->is_ref)
    {
      // Take a copy of RHS for LHS
      *p_lhs = zvp_clone_ex (rhs);
    }
  else
    {
      // Share a copy
      rhs->refcount++;
      *p_lhs = rhs;
    }

    }

    }

    assert (IS_BOOL >= 0 && IS_BOOL <= 6);
  if ((*p_lhs)->type != IS_BOOL)
  {
    sep_copy_on_write (p_lhs);
    convert_to_boolean (*p_lhs);
  }

phc_check_invariants (TSRMLS_C);
}
// if (TLE196) goto L343 else goto L344;
{
     zval* p_cond;
  if (local_TLE196 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE196;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L343;
   else
      goto L344;
phc_check_invariants (TSRMLS_C);
}
// L343:
L343:;
// $TSt197 = $this->_buffer;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_buffer", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt197 == NULL)
    {
      local_TSt197 = EG (uninitialized_zval_ptr);
      local_TSt197->refcount++;
    }
  zval** p_lhs = &local_TSt197;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// return $TSt197;
{
     zval* rhs;
  if (local_TSt197 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TSt197;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// goto L345;
{
goto L345;
phc_check_invariants (TSRMLS_C);
}
// L344:
L344:;
// $TLE198 = -1;
{
        if (local_TLE198 == NULL)
    {
      local_TLE198 = EG (uninitialized_zval_ptr);
      local_TLE198->refcount++;
    }
  zval** p_lhs = &local_TLE198;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, -1);

phc_check_invariants (TSRMLS_C);
}
// $TLE199 = ($endPos == $TLE198);
{
    if (local_TLE199 == NULL)
    {
      local_TLE199 = EG (uninitialized_zval_ptr);
      local_TLE199->refcount++;
    }
  zval** p_lhs = &local_TLE199;

    zval* left;
  if (local_endPos == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_endPos;

    zval* right;
  if (local_TLE198 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE198;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_equal_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE199) goto L337 else goto L338;
{
     zval* p_cond;
  if (local_TLE199 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE199;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L337;
   else
      goto L338;
phc_check_invariants (TSRMLS_C);
}
// L337:
L337:;
// $TSt200 = $this->_pos;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_pos", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt200 == NULL)
    {
      local_TSt200 = EG (uninitialized_zval_ptr);
      local_TSt200->refcount++;
    }
  zval** p_lhs = &local_TSt200;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $endPos = $TSt200;
{
    if (local_endPos == NULL)
    {
      local_endPos = EG (uninitialized_zval_ptr);
      local_endPos->refcount++;
    }
  zval** p_lhs = &local_endPos;

    zval* rhs;
  if (local_TSt200 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TSt200;

  if (*p_lhs != rhs)
    {
        if ((*p_lhs)->is_ref)
      overwrite_lhs (*p_lhs, rhs);
  else
    {
      zval_ptr_dtor (p_lhs);
        if (rhs->is_ref)
    {
      // Take a copy of RHS for LHS
      *p_lhs = zvp_clone_ex (rhs);
    }
  else
    {
      // Share a copy
      rhs->refcount++;
      *p_lhs = rhs;
    }

    }

    }
phc_check_invariants (TSRMLS_C);
}
// goto L339;
{
goto L339;
phc_check_invariants (TSRMLS_C);
}
// L338:
L338:;
// goto L339;
{
goto L339;
phc_check_invariants (TSRMLS_C);
}
// L339:
L339:;
// $TLE317 = param_is_ref (NULL, "substr", 0);
{
   initialize_function_call (&substr_fci, &substr_fcic, "substr", "<unknown>", 0 TSRMLS_CC);
		zend_function* signature = substr_fcic.function_handler;
	zend_arg_info* arg_info = signature->common.arg_info;
	int count = 0;
	while (arg_info && count < 0)
	{
		count++;
		arg_info++;
	}

	  if (local_TLE317 == NULL)
    {
      local_TLE317 = EG (uninitialized_zval_ptr);
      local_TLE317->refcount++;
    }
  zval** p_lhs = &local_TLE317;

	zval* rhs;
	ALLOC_INIT_ZVAL (rhs);
	if (arg_info && count == 0)
	{
		ZVAL_BOOL (rhs, arg_info->pass_by_reference);
	}
	else
	{
		ZVAL_BOOL (rhs, signature->common.pass_rest_by_reference);
	}
	write_var (p_lhs, rhs);
	zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// if (TLE317) goto L340 else goto L341;
{
     zval* p_cond;
  if (local_TLE317 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE317;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L340;
   else
      goto L341;
phc_check_invariants (TSRMLS_C);
}
// L340:
L340:;
// $TMIt316 =& $this->_buffer;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_buffer", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TMIt316 == NULL)
    {
      local_TMIt316 = EG (uninitialized_zval_ptr);
      local_TMIt316->refcount++;
    }
  zval** p_lhs = &local_TMIt316;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// goto L342;
{
goto L342;
phc_check_invariants (TSRMLS_C);
}
// L341:
L341:;
// $TMIt316 = $this->_buffer;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_buffer", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TMIt316 == NULL)
    {
      local_TMIt316 = EG (uninitialized_zval_ptr);
      local_TMIt316->refcount++;
    }
  zval** p_lhs = &local_TMIt316;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// goto L342;
{
goto L342;
phc_check_invariants (TSRMLS_C);
}
// L342:
L342:;
// $TLE201 = substr($TMIt316, $startPos, $endPos);
{
   initialize_function_call (&substr_fci, &substr_fcic, "substr", "tools.source.php", 162 TSRMLS_CC);
      zend_function* signature = substr_fcic.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[3];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;
   // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;
   // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [3];
   zval* args [3];
   zval** args_ind [3];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TMIt316 == NULL)
    {
      local_TMIt316 = EG (uninitialized_zval_ptr);
      local_TMIt316->refcount++;
    }
  zval** p_arg = &local_TMIt316;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TMIt316 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TMIt316;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;
   destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_startPos == NULL)
    {
      local_startPos = EG (uninitialized_zval_ptr);
      local_startPos->refcount++;
    }
  zval** p_arg = &local_startPos;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_startPos == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_startPos;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;
   destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_endPos == NULL)
    {
      local_endPos = EG (uninitialized_zval_ptr);
      local_endPos->refcount++;
    }
  zval** p_arg = &local_endPos;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_endPos == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_endPos;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 162, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = substr_fci.param_count;
   zval*** params_save = substr_fci.params;
   zval** retval_save = substr_fci.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   substr_fci.params = args_ind;
   substr_fci.param_count = 3;
   substr_fci.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&substr_fci, &substr_fcic TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   substr_fci.params = params_save;
   substr_fci.param_count = param_count_save;
   substr_fci.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 3; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE201 == NULL)
    {
      local_TLE201 = EG (uninitialized_zval_ptr);
      local_TLE201->refcount++;
    }
  zval** p_lhs = &local_TLE201;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// return $TLE201;
{
     zval* rhs;
  if (local_TLE201 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE201;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// goto L345;
{
goto L345;
phc_check_invariants (TSRMLS_C);
}
// L345:
L345:;
// Method exit
end_of_function:__attribute__((unused));
if (local_TEF4 != NULL)
{
zval_ptr_dtor (&local_TEF4);
}
if (local_TLE194 != NULL)
{
zval_ptr_dtor (&local_TLE194);
}
if (local_TLE195 != NULL)
{
zval_ptr_dtor (&local_TLE195);
}
if (local_TLE196 != NULL)
{
zval_ptr_dtor (&local_TLE196);
}
if (local_TLE198 != NULL)
{
zval_ptr_dtor (&local_TLE198);
}
if (local_TLE199 != NULL)
{
zval_ptr_dtor (&local_TLE199);
}
if (local_TLE201 != NULL)
{
zval_ptr_dtor (&local_TLE201);
}
if (local_TLE3 != NULL)
{
zval_ptr_dtor (&local_TLE3);
}
if (local_TLE317 != NULL)
{
zval_ptr_dtor (&local_TLE317);
}
if (local_TMIt316 != NULL)
{
zval_ptr_dtor (&local_TMIt316);
}
if (local_TSt197 != NULL)
{
zval_ptr_dtor (&local_TSt197);
}
if (local_TSt200 != NULL)
{
zval_ptr_dtor (&local_TSt200);
}
if (local_endPos != NULL)
{
zval_ptr_dtor (&local_endPos);
}
if (local_startPos != NULL)
{
zval_ptr_dtor (&local_startPos);
}
}
// public function getMemoryLength()
// {
// 	$TSt202 = $this->_pos;
// 	return $TSt202;
// }
PHP_METHOD(Memory, getMemoryLength)
{
zval* local_TSt202 = NULL;
zval* local_this = getThis();
// Function body
// $TSt202 = $this->_pos;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_pos", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt202 == NULL)
    {
      local_TSt202 = EG (uninitialized_zval_ptr);
      local_TSt202->refcount++;
    }
  zval** p_lhs = &local_TSt202;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// return $TSt202;
{
     zval* rhs;
  if (local_TSt202 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TSt202;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TSt202 != NULL)
{
zval_ptr_dtor (&local_TSt202);
}
}
// public function setMemorySize($size)
// {
// 	$TSt203 = $this->_pos;
// 	$TLE204 = ($TSt203 < $size);
// 	if (TLE204) goto L360 else goto L361;
// L360:
// 	$TLE205 = 0;
// 	$TLE206 = ($TLE205 < $size);
// 	if (TLE206) goto L354 else goto L355;
// L354:
// 	$TSt207 = $this->_pos;
// 	$pos = $TSt207;
// 	$i = 0;
// 	$ElcfPF1 = True;
// L353:
// 	if (ElcfPF1) goto L346 else goto L347;
// L346:
// 	$ElcfPF1 = False;
// 	goto L348;
// L347:
// 	++$i;
// 	goto L348;
// L348:
// 	$TLE208 = ($size - $pos);
// 	$TLE209 = ($i < $TLE208);
// 	if (TLE209) goto L350 else goto L351;
// L350:
// 	goto L352;
// L351:
// 	goto L349;
// 	goto L352;
// L352:
// 	$TLE210 = 255;
// 	$this->addByte($TLE210);
// 	goto L353;
// L349:
// 	goto L356;
// L355:
// 	goto L356;
// L356:
// 	goto L362;
// L361:
// 	$TLE211 = 0;
// 	$TLE319 = param_is_ref (NULL, "substr", 0);
// 	if (TLE319) goto L357 else goto L358;
// L357:
// 	$TMIt318 =& $this->_buffer;
// 	goto L359;
// L358:
// 	$TMIt318 = $this->_buffer;
// 	goto L359;
// L359:
// 	$TLE212 = substr($TMIt318, $TLE211, $size);
// 	$this->_buffer = $TLE212;
// 	$this->_pos = $size;
// 	goto L362;
// L362:
// }
PHP_METHOD(Memory, setMemorySize)
{
zval* local_ElcfPF1 = NULL;
zval* local_TLE204 = NULL;
zval* local_TLE205 = NULL;
zval* local_TLE206 = NULL;
zval* local_TLE208 = NULL;
zval* local_TLE209 = NULL;
zval* local_TLE210 = NULL;
zval* local_TLE211 = NULL;
zval* local_TLE212 = NULL;
zval* local_TLE319 = NULL;
zval* local_TMIt318 = NULL;
zval* local_TSt203 = NULL;
zval* local_TSt207 = NULL;
zval* local_i = NULL;
zval* local_pos = NULL;
zval* local_size = NULL;
zval* local_this = getThis();
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_size != NULL)
{
	zval_ptr_dtor (&local_size);
}
local_size = params[0];
}
// Function body
// $TSt203 = $this->_pos;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_pos", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt203 == NULL)
    {
      local_TSt203 = EG (uninitialized_zval_ptr);
      local_TSt203->refcount++;
    }
  zval** p_lhs = &local_TSt203;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE204 = ($TSt203 < $size);
{
    if (local_TLE204 == NULL)
    {
      local_TLE204 = EG (uninitialized_zval_ptr);
      local_TLE204->refcount++;
    }
  zval** p_lhs = &local_TLE204;

    zval* left;
  if (local_TSt203 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt203;

    zval* right;
  if (local_size == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_size;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_smaller_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE204) goto L360 else goto L361;
{
     zval* p_cond;
  if (local_TLE204 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE204;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L360;
   else
      goto L361;
phc_check_invariants (TSRMLS_C);
}
// L360:
L360:;
// $TLE205 = 0;
{
        if (local_TLE205 == NULL)
    {
      local_TLE205 = EG (uninitialized_zval_ptr);
      local_TLE205->refcount++;
    }
  zval** p_lhs = &local_TLE205;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $TLE206 = ($TLE205 < $size);
{
    if (local_TLE206 == NULL)
    {
      local_TLE206 = EG (uninitialized_zval_ptr);
      local_TLE206->refcount++;
    }
  zval** p_lhs = &local_TLE206;

    zval* left;
  if (local_TLE205 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE205;

    zval* right;
  if (local_size == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_size;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_smaller_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE206) goto L354 else goto L355;
{
     zval* p_cond;
  if (local_TLE206 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE206;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L354;
   else
      goto L355;
phc_check_invariants (TSRMLS_C);
}
// L354:
L354:;
// $TSt207 = $this->_pos;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_pos", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt207 == NULL)
    {
      local_TSt207 = EG (uninitialized_zval_ptr);
      local_TSt207->refcount++;
    }
  zval** p_lhs = &local_TSt207;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $pos = $TSt207;
{
    if (local_pos == NULL)
    {
      local_pos = EG (uninitialized_zval_ptr);
      local_pos->refcount++;
    }
  zval** p_lhs = &local_pos;

    zval* rhs;
  if (local_TSt207 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TSt207;

  if (*p_lhs != rhs)
    {
        if ((*p_lhs)->is_ref)
      overwrite_lhs (*p_lhs, rhs);
  else
    {
      zval_ptr_dtor (p_lhs);
        if (rhs->is_ref)
    {
      // Take a copy of RHS for LHS
      *p_lhs = zvp_clone_ex (rhs);
    }
  else
    {
      // Share a copy
      rhs->refcount++;
      *p_lhs = rhs;
    }

    }

    }
phc_check_invariants (TSRMLS_C);
}
// $i = 0;
{
        if (local_i == NULL)
    {
      local_i = EG (uninitialized_zval_ptr);
      local_i->refcount++;
    }
  zval** p_lhs = &local_i;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $ElcfPF1 = True;
{
        if (local_ElcfPF1 == NULL)
    {
      local_ElcfPF1 = EG (uninitialized_zval_ptr);
      local_ElcfPF1->refcount++;
    }
  zval** p_lhs = &local_ElcfPF1;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_BOOL (value, 1);

phc_check_invariants (TSRMLS_C);
}
// L353:
L353:;
// if (ElcfPF1) goto L346 else goto L347;
{
     zval* p_cond;
  if (local_ElcfPF1 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_ElcfPF1;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L346;
   else
      goto L347;
phc_check_invariants (TSRMLS_C);
}
// L346:
L346:;
// $ElcfPF1 = False;
{
        if (local_ElcfPF1 == NULL)
    {
      local_ElcfPF1 = EG (uninitialized_zval_ptr);
      local_ElcfPF1->refcount++;
    }
  zval** p_lhs = &local_ElcfPF1;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_BOOL (value, 0);

phc_check_invariants (TSRMLS_C);
}
// goto L348;
{
goto L348;
phc_check_invariants (TSRMLS_C);
}
// L347:
L347:;
// ++$i;
{
     if (local_i == NULL)
    {
      local_i = EG (uninitialized_zval_ptr);
      local_i->refcount++;
    }
  zval** p_var = &local_i;

   sep_copy_on_write (p_var);
   increment_function (*p_var);
phc_check_invariants (TSRMLS_C);
}
// goto L348;
{
goto L348;
phc_check_invariants (TSRMLS_C);
}
// L348:
L348:;
// $TLE208 = ($size - $pos);
{
    if (local_TLE208 == NULL)
    {
      local_TLE208 = EG (uninitialized_zval_ptr);
      local_TLE208->refcount++;
    }
  zval** p_lhs = &local_TLE208;

    zval* left;
  if (local_size == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_size;

    zval* right;
  if (local_pos == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_pos;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE209 = ($i < $TLE208);
{
    if (local_TLE209 == NULL)
    {
      local_TLE209 = EG (uninitialized_zval_ptr);
      local_TLE209->refcount++;
    }
  zval** p_lhs = &local_TLE209;

    zval* left;
  if (local_i == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_i;

    zval* right;
  if (local_TLE208 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE208;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_smaller_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE209) goto L350 else goto L351;
{
     zval* p_cond;
  if (local_TLE209 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE209;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L350;
   else
      goto L351;
phc_check_invariants (TSRMLS_C);
}
// L350:
L350:;
// goto L352;
{
goto L352;
phc_check_invariants (TSRMLS_C);
}
// L351:
L351:;
// goto L349;
{
goto L349;
phc_check_invariants (TSRMLS_C);
}
// goto L352;
{
goto L352;
phc_check_invariants (TSRMLS_C);
}
// L352:
L352:;
// $TLE210 = 255;
{
        if (local_TLE210 == NULL)
    {
      local_TLE210 = EG (uninitialized_zval_ptr);
      local_TLE210->refcount++;
    }
  zval** p_lhs = &local_TLE210;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $this->addByte($TLE210);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "addByte", "tools.source.php", 173 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE210 == NULL)
    {
      local_TLE210 = EG (uninitialized_zval_ptr);
      local_TLE210->refcount++;
    }
  zval** p_arg = &local_TLE210;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE210 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE210;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 173, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// goto L353;
{
goto L353;
phc_check_invariants (TSRMLS_C);
}
// L349:
L349:;
// goto L356;
{
goto L356;
phc_check_invariants (TSRMLS_C);
}
// L355:
L355:;
// goto L356;
{
goto L356;
phc_check_invariants (TSRMLS_C);
}
// L356:
L356:;
// goto L362;
{
goto L362;
phc_check_invariants (TSRMLS_C);
}
// L361:
L361:;
// $TLE211 = 0;
{
        if (local_TLE211 == NULL)
    {
      local_TLE211 = EG (uninitialized_zval_ptr);
      local_TLE211->refcount++;
    }
  zval** p_lhs = &local_TLE211;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $TLE319 = param_is_ref (NULL, "substr", 0);
{
   initialize_function_call (&substr_fci, &substr_fcic, "substr", "<unknown>", 0 TSRMLS_CC);
		zend_function* signature = substr_fcic.function_handler;
	zend_arg_info* arg_info = signature->common.arg_info;
	int count = 0;
	while (arg_info && count < 0)
	{
		count++;
		arg_info++;
	}

	  if (local_TLE319 == NULL)
    {
      local_TLE319 = EG (uninitialized_zval_ptr);
      local_TLE319->refcount++;
    }
  zval** p_lhs = &local_TLE319;

	zval* rhs;
	ALLOC_INIT_ZVAL (rhs);
	if (arg_info && count == 0)
	{
		ZVAL_BOOL (rhs, arg_info->pass_by_reference);
	}
	else
	{
		ZVAL_BOOL (rhs, signature->common.pass_rest_by_reference);
	}
	write_var (p_lhs, rhs);
	zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// if (TLE319) goto L357 else goto L358;
{
     zval* p_cond;
  if (local_TLE319 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE319;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L357;
   else
      goto L358;
phc_check_invariants (TSRMLS_C);
}
// L357:
L357:;
// $TMIt318 =& $this->_buffer;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_buffer", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TMIt318 == NULL)
    {
      local_TMIt318 = EG (uninitialized_zval_ptr);
      local_TMIt318->refcount++;
    }
  zval** p_lhs = &local_TMIt318;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// goto L359;
{
goto L359;
phc_check_invariants (TSRMLS_C);
}
// L358:
L358:;
// $TMIt318 = $this->_buffer;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_buffer", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TMIt318 == NULL)
    {
      local_TMIt318 = EG (uninitialized_zval_ptr);
      local_TMIt318->refcount++;
    }
  zval** p_lhs = &local_TMIt318;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// goto L359;
{
goto L359;
phc_check_invariants (TSRMLS_C);
}
// L359:
L359:;
// $TLE212 = substr($TMIt318, $TLE211, $size);
{
   initialize_function_call (&substr_fci, &substr_fcic, "substr", "tools.source.php", 178 TSRMLS_CC);
      zend_function* signature = substr_fcic.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[3];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;
   // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;
   // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [3];
   zval* args [3];
   zval** args_ind [3];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TMIt318 == NULL)
    {
      local_TMIt318 = EG (uninitialized_zval_ptr);
      local_TMIt318->refcount++;
    }
  zval** p_arg = &local_TMIt318;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TMIt318 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TMIt318;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;
   destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE211 == NULL)
    {
      local_TLE211 = EG (uninitialized_zval_ptr);
      local_TLE211->refcount++;
    }
  zval** p_arg = &local_TLE211;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE211 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE211;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;
   destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_size == NULL)
    {
      local_size = EG (uninitialized_zval_ptr);
      local_size->refcount++;
    }
  zval** p_arg = &local_size;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_size == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_size;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 178, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = substr_fci.param_count;
   zval*** params_save = substr_fci.params;
   zval** retval_save = substr_fci.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   substr_fci.params = args_ind;
   substr_fci.param_count = 3;
   substr_fci.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&substr_fci, &substr_fcic TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   substr_fci.params = params_save;
   substr_fci.param_count = param_count_save;
   substr_fci.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 3; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE212 == NULL)
    {
      local_TLE212 = EG (uninitialized_zval_ptr);
      local_TLE212->refcount++;
    }
  zval** p_lhs = &local_TLE212;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $this->_buffer = $TLE212;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE212 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE212;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_buffer", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// $this->_pos = $size;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_size == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_size;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_pos", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// goto L362;
{
goto L362;
phc_check_invariants (TSRMLS_C);
}
// L362:
L362:;
// Method exit
end_of_function:__attribute__((unused));
if (local_ElcfPF1 != NULL)
{
zval_ptr_dtor (&local_ElcfPF1);
}
if (local_TLE204 != NULL)
{
zval_ptr_dtor (&local_TLE204);
}
if (local_TLE205 != NULL)
{
zval_ptr_dtor (&local_TLE205);
}
if (local_TLE206 != NULL)
{
zval_ptr_dtor (&local_TLE206);
}
if (local_TLE208 != NULL)
{
zval_ptr_dtor (&local_TLE208);
}
if (local_TLE209 != NULL)
{
zval_ptr_dtor (&local_TLE209);
}
if (local_TLE210 != NULL)
{
zval_ptr_dtor (&local_TLE210);
}
if (local_TLE211 != NULL)
{
zval_ptr_dtor (&local_TLE211);
}
if (local_TLE212 != NULL)
{
zval_ptr_dtor (&local_TLE212);
}
if (local_TLE319 != NULL)
{
zval_ptr_dtor (&local_TLE319);
}
if (local_TMIt318 != NULL)
{
zval_ptr_dtor (&local_TMIt318);
}
if (local_TSt203 != NULL)
{
zval_ptr_dtor (&local_TSt203);
}
if (local_TSt207 != NULL)
{
zval_ptr_dtor (&local_TSt207);
}
if (local_i != NULL)
{
zval_ptr_dtor (&local_i);
}
if (local_pos != NULL)
{
zval_ptr_dtor (&local_pos);
}
if (local_size != NULL)
{
zval_ptr_dtor (&local_size);
}
}
// public function resetMemory()
// {
// 	$TLE213 = '';
// 	$this->_buffer = $TLE213;
// 	$TLE214 = '';
// 	$this->_mem = $TLE214;
// 	$TLE215 = 0;
// 	$this->_pos = $TLE215;
// 	$TLE216 = 0;
// 	$this->_readPos = $TLE216;
// }
PHP_METHOD(Memory, resetMemory)
{
zval* local_TLE213 = NULL;
zval* local_TLE214 = NULL;
zval* local_TLE215 = NULL;
zval* local_TLE216 = NULL;
zval* local_this = getThis();
// Function body
// $TLE213 = '';
{
        if (local_TLE213 == NULL)
    {
      local_TLE213 = EG (uninitialized_zval_ptr);
      local_TLE213->refcount++;
    }
  zval** p_lhs = &local_TLE213;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_STRINGL(value, "", 0, 1);

phc_check_invariants (TSRMLS_C);
}
// $this->_buffer = $TLE213;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE213 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE213;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_buffer", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// $TLE214 = '';
{
        if (local_TLE214 == NULL)
    {
      local_TLE214 = EG (uninitialized_zval_ptr);
      local_TLE214->refcount++;
    }
  zval** p_lhs = &local_TLE214;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_STRINGL(value, "", 0, 1);

phc_check_invariants (TSRMLS_C);
}
// $this->_mem = $TLE214;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE214 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE214;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_mem", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// $TLE215 = 0;
{
        if (local_TLE215 == NULL)
    {
      local_TLE215 = EG (uninitialized_zval_ptr);
      local_TLE215->refcount++;
    }
  zval** p_lhs = &local_TLE215;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $this->_pos = $TLE215;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE215 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE215;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_pos", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// $TLE216 = 0;
{
        if (local_TLE216 == NULL)
    {
      local_TLE216 = EG (uninitialized_zval_ptr);
      local_TLE216->refcount++;
    }
  zval** p_lhs = &local_TLE216;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $this->_readPos = $TLE216;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE216 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE216;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_readPos", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE213 != NULL)
{
zval_ptr_dtor (&local_TLE213);
}
if (local_TLE214 != NULL)
{
zval_ptr_dtor (&local_TLE214);
}
if (local_TLE215 != NULL)
{
zval_ptr_dtor (&local_TLE215);
}
if (local_TLE216 != NULL)
{
zval_ptr_dtor (&local_TLE216);
}
}
// public function dumpMemory()
// {
// 	$i = 0;
// 	$ElcfPF2 = True;
// L376:
// 	if (ElcfPF2) goto L363 else goto L364;
// L363:
// 	$ElcfPF2 = False;
// 	goto L365;
// L364:
// 	++$i;
// 	goto L365;
// L365:
// 	$TSt217 = $this->_pos;
// 	$TLE218 = ($i < $TSt217);
// 	if (TLE218) goto L367 else goto L368;
// L367:
// 	goto L369;
// L368:
// 	goto L366;
// 	goto L369;
// L369:
// 	$TLE219 = '%02X ';
// 	$TLE322 = param_is_ref (NULL, "printf", 0);
// 	if (TLE322) goto L370 else goto L371;
// L370:
// 	$TMIt320 =& $this->_mem;
// 	$TMIi321 =& $TMIt320[$i];
// 	goto L372;
// L371:
// 	$TMIt320 = $this->_mem;
// 	$TMIi321 = $TMIt320[$i];
// 	goto L372;
// L372:
// 	printf($TLE219, $TMIi321);
// 	$TLE220 = 1;
// 	$TLE221 = ($i + $TLE220);
// 	$TLE222 = 50;
// 	$TLE223 = ($TLE221 % $TLE222);
// 	$TLE224 = 0;
// 	$TLE225 = ($TLE223 == $TLE224);
// 	if (TLE225) goto L373 else goto L374;
// L373:
// 	$TLE226 = '
// ';
// 	printf($TLE226);
// 	goto L375;
// L374:
// 	goto L375;
// L375:
// 	goto L376;
// L366:
// 	$TLE227 = 1;
// 	$TLE228 = ($i + $TLE227);
// 	$TLE229 = 50;
// 	$TLE230 = ($TLE228 % $TLE229);
// 	$TLE231 = 0;
// 	$TLE232 = ($TLE230 != $TLE231);
// 	if (TLE232) goto L377 else goto L378;
// L377:
// 	$TLE233 = '
// ';
// 	printf($TLE233);
// 	goto L379;
// L378:
// 	goto L379;
// L379:
// }
PHP_METHOD(Memory, dumpMemory)
{
zval* local_ElcfPF2 = NULL;
zval* local_TLE218 = NULL;
zval* local_TLE219 = NULL;
zval* local_TLE220 = NULL;
zval* local_TLE221 = NULL;
zval* local_TLE222 = NULL;
zval* local_TLE223 = NULL;
zval* local_TLE224 = NULL;
zval* local_TLE225 = NULL;
zval* local_TLE226 = NULL;
zval* local_TLE227 = NULL;
zval* local_TLE228 = NULL;
zval* local_TLE229 = NULL;
zval* local_TLE230 = NULL;
zval* local_TLE231 = NULL;
zval* local_TLE232 = NULL;
zval* local_TLE233 = NULL;
zval* local_TLE322 = NULL;
zval* local_TMIi321 = NULL;
zval* local_TMIt320 = NULL;
zval* local_TSt217 = NULL;
zval* local_i = NULL;
zval* local_this = getThis();
// Function body
// $i = 0;
{
        if (local_i == NULL)
    {
      local_i = EG (uninitialized_zval_ptr);
      local_i->refcount++;
    }
  zval** p_lhs = &local_i;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $ElcfPF2 = True;
{
        if (local_ElcfPF2 == NULL)
    {
      local_ElcfPF2 = EG (uninitialized_zval_ptr);
      local_ElcfPF2->refcount++;
    }
  zval** p_lhs = &local_ElcfPF2;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_BOOL (value, 1);

phc_check_invariants (TSRMLS_C);
}
// L376:
L376:;
// if (ElcfPF2) goto L363 else goto L364;
{
     zval* p_cond;
  if (local_ElcfPF2 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_ElcfPF2;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L363;
   else
      goto L364;
phc_check_invariants (TSRMLS_C);
}
// L363:
L363:;
// $ElcfPF2 = False;
{
        if (local_ElcfPF2 == NULL)
    {
      local_ElcfPF2 = EG (uninitialized_zval_ptr);
      local_ElcfPF2->refcount++;
    }
  zval** p_lhs = &local_ElcfPF2;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_BOOL (value, 0);

phc_check_invariants (TSRMLS_C);
}
// goto L365;
{
goto L365;
phc_check_invariants (TSRMLS_C);
}
// L364:
L364:;
// ++$i;
{
     if (local_i == NULL)
    {
      local_i = EG (uninitialized_zval_ptr);
      local_i->refcount++;
    }
  zval** p_var = &local_i;

   sep_copy_on_write (p_var);
   increment_function (*p_var);
phc_check_invariants (TSRMLS_C);
}
// goto L365;
{
goto L365;
phc_check_invariants (TSRMLS_C);
}
// L365:
L365:;
// $TSt217 = $this->_pos;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_pos", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt217 == NULL)
    {
      local_TSt217 = EG (uninitialized_zval_ptr);
      local_TSt217->refcount++;
    }
  zval** p_lhs = &local_TSt217;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE218 = ($i < $TSt217);
{
    if (local_TLE218 == NULL)
    {
      local_TLE218 = EG (uninitialized_zval_ptr);
      local_TLE218->refcount++;
    }
  zval** p_lhs = &local_TLE218;

    zval* left;
  if (local_i == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_i;

    zval* right;
  if (local_TSt217 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TSt217;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_smaller_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE218) goto L367 else goto L368;
{
     zval* p_cond;
  if (local_TLE218 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE218;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L367;
   else
      goto L368;
phc_check_invariants (TSRMLS_C);
}
// L367:
L367:;
// goto L369;
{
goto L369;
phc_check_invariants (TSRMLS_C);
}
// L368:
L368:;
// goto L366;
{
goto L366;
phc_check_invariants (TSRMLS_C);
}
// goto L369;
{
goto L369;
phc_check_invariants (TSRMLS_C);
}
// L369:
L369:;
// $TLE219 = '%02X ';
{
        if (local_TLE219 == NULL)
    {
      local_TLE219 = EG (uninitialized_zval_ptr);
      local_TLE219->refcount++;
    }
  zval** p_lhs = &local_TLE219;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_STRINGL(value, "%02X ", 5, 1);

phc_check_invariants (TSRMLS_C);
}
// $TLE322 = param_is_ref (NULL, "printf", 0);
{
   initialize_function_call (&printf_fci, &printf_fcic, "printf", "<unknown>", 0 TSRMLS_CC);
		zend_function* signature = printf_fcic.function_handler;
	zend_arg_info* arg_info = signature->common.arg_info;
	int count = 0;
	while (arg_info && count < 0)
	{
		count++;
		arg_info++;
	}

	  if (local_TLE322 == NULL)
    {
      local_TLE322 = EG (uninitialized_zval_ptr);
      local_TLE322->refcount++;
    }
  zval** p_lhs = &local_TLE322;

	zval* rhs;
	ALLOC_INIT_ZVAL (rhs);
	if (arg_info && count == 0)
	{
		ZVAL_BOOL (rhs, arg_info->pass_by_reference);
	}
	else
	{
		ZVAL_BOOL (rhs, signature->common.pass_rest_by_reference);
	}
	write_var (p_lhs, rhs);
	zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// if (TLE322) goto L370 else goto L371;
{
     zval* p_cond;
  if (local_TLE322 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE322;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L370;
   else
      goto L371;
phc_check_invariants (TSRMLS_C);
}
// L370:
L370:;
// $TMIt320 =& $this->_mem;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_mem", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TMIt320 == NULL)
    {
      local_TMIt320 = EG (uninitialized_zval_ptr);
      local_TMIt320->refcount++;
    }
  zval** p_lhs = &local_TMIt320;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// $TMIi321 =& $TMIt320[$i];
{
     if (local_TMIi321 == NULL)
    {
      local_TMIi321 = EG (uninitialized_zval_ptr);
      local_TMIi321->refcount++;
    }
  zval** p_lhs = &local_TMIi321;

     if (local_TMIt320 == NULL)
    {
      local_TMIt320 = EG (uninitialized_zval_ptr);
      local_TMIt320->refcount++;
    }
  zval** p_r_array = &local_TMIt320;

     zval* r_index;
  if (local_i == NULL)
    r_index = EG (uninitialized_zval_ptr);
  else
    r_index = local_i;

   check_array_type (p_r_array TSRMLS_CC);
   zval** p_rhs = get_ht_entry (p_r_array, r_index TSRMLS_CC);
   sep_copy_on_write (p_rhs);
   copy_into_ref (p_lhs, p_rhs);
phc_check_invariants (TSRMLS_C);
}
// goto L372;
{
goto L372;
phc_check_invariants (TSRMLS_C);
}
// L371:
L371:;
// $TMIt320 = $this->_mem;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_mem", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TMIt320 == NULL)
    {
      local_TMIt320 = EG (uninitialized_zval_ptr);
      local_TMIt320->refcount++;
    }
  zval** p_lhs = &local_TMIt320;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TMIi321 = $TMIt320[$i];
{
     if (local_TMIi321 == NULL)
    {
      local_TMIi321 = EG (uninitialized_zval_ptr);
      local_TMIi321->refcount++;
    }
  zval** p_lhs = &local_TMIi321;

     zval* r_array;
  if (local_TMIt320 == NULL)
    r_array = EG (uninitialized_zval_ptr);
  else
    r_array = local_TMIt320;

     zval* r_index;
  if (local_i == NULL)
    r_index = EG (uninitialized_zval_ptr);
  else
    r_index = local_i;


   zval* rhs;
   int is_rhs_new = 0;
    if (Z_TYPE_P (r_array) != IS_ARRAY)
    {
      if (Z_TYPE_P (r_array) == IS_STRING)
	{
	  is_rhs_new = 1;
	  rhs = read_string_index (r_array, r_index TSRMLS_CC);
	}
      else
	// TODO: warning here?
	rhs = EG (uninitialized_zval_ptr);
    }
    else
    {
      if (check_array_index_type (r_index TSRMLS_CC))
	{
	  // Read array variable
	  read_array (&rhs, r_array, r_index TSRMLS_CC);
	}
      else
	rhs = *p_lhs; // HACK to fail  *p_lhs != rhs
    }

   if (*p_lhs != rhs)
      write_var (p_lhs, rhs);

   if (is_rhs_new) zval_ptr_dtor (&rhs);
phc_check_invariants (TSRMLS_C);
}
// goto L372;
{
goto L372;
phc_check_invariants (TSRMLS_C);
}
// L372:
L372:;
// printf($TLE219, $TMIi321);
{
   initialize_function_call (&printf_fci, &printf_fcic, "printf", "tools.source.php", 190 TSRMLS_CC);
      zend_function* signature = printf_fcic.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[2];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;
   // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [2];
   zval* args [2];
   zval** args_ind [2];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE219 == NULL)
    {
      local_TLE219 = EG (uninitialized_zval_ptr);
      local_TLE219->refcount++;
    }
  zval** p_arg = &local_TLE219;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE219 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE219;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;
   destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TMIi321 == NULL)
    {
      local_TMIi321 = EG (uninitialized_zval_ptr);
      local_TMIi321->refcount++;
    }
  zval** p_arg = &local_TMIi321;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TMIi321 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TMIi321;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 190, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = printf_fci.param_count;
   zval*** params_save = printf_fci.params;
   zval** retval_save = printf_fci.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   printf_fci.params = args_ind;
   printf_fci.param_count = 2;
   printf_fci.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&printf_fci, &printf_fcic TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   printf_fci.params = params_save;
   printf_fci.param_count = param_count_save;
   printf_fci.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 2; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE220 = 1;
{
        if (local_TLE220 == NULL)
    {
      local_TLE220 = EG (uninitialized_zval_ptr);
      local_TLE220->refcount++;
    }
  zval** p_lhs = &local_TLE220;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 1);

phc_check_invariants (TSRMLS_C);
}
// $TLE221 = ($i + $TLE220);
{
    if (local_TLE221 == NULL)
    {
      local_TLE221 = EG (uninitialized_zval_ptr);
      local_TLE221->refcount++;
    }
  zval** p_lhs = &local_TLE221;

    zval* left;
  if (local_i == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_i;

    zval* right;
  if (local_TLE220 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE220;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE222 = 50;
{
        if (local_TLE222 == NULL)
    {
      local_TLE222 = EG (uninitialized_zval_ptr);
      local_TLE222->refcount++;
    }
  zval** p_lhs = &local_TLE222;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 50);

phc_check_invariants (TSRMLS_C);
}
// $TLE223 = ($TLE221 % $TLE222);
{
    if (local_TLE223 == NULL)
    {
      local_TLE223 = EG (uninitialized_zval_ptr);
      local_TLE223->refcount++;
    }
  zval** p_lhs = &local_TLE223;

    zval* left;
  if (local_TLE221 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE221;

    zval* right;
  if (local_TLE222 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE222;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  mod_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE224 = 0;
{
        if (local_TLE224 == NULL)
    {
      local_TLE224 = EG (uninitialized_zval_ptr);
      local_TLE224->refcount++;
    }
  zval** p_lhs = &local_TLE224;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $TLE225 = ($TLE223 == $TLE224);
{
    if (local_TLE225 == NULL)
    {
      local_TLE225 = EG (uninitialized_zval_ptr);
      local_TLE225->refcount++;
    }
  zval** p_lhs = &local_TLE225;

    zval* left;
  if (local_TLE223 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE223;

    zval* right;
  if (local_TLE224 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE224;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_equal_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE225) goto L373 else goto L374;
{
     zval* p_cond;
  if (local_TLE225 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE225;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L373;
   else
      goto L374;
phc_check_invariants (TSRMLS_C);
}
// L373:
L373:;
// $TLE226 = '
// ';
{
        if (local_TLE226 == NULL)
    {
      local_TLE226 = EG (uninitialized_zval_ptr);
      local_TLE226->refcount++;
    }
  zval** p_lhs = &local_TLE226;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_STRINGL(value, "\012", 1, 1);

phc_check_invariants (TSRMLS_C);
}
// printf($TLE226);
{
   initialize_function_call (&printf_fci, &printf_fcic, "printf", "tools.source.php", 192 TSRMLS_CC);
      zend_function* signature = printf_fcic.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE226 == NULL)
    {
      local_TLE226 = EG (uninitialized_zval_ptr);
      local_TLE226->refcount++;
    }
  zval** p_arg = &local_TLE226;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE226 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE226;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 192, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = printf_fci.param_count;
   zval*** params_save = printf_fci.params;
   zval** retval_save = printf_fci.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   printf_fci.params = args_ind;
   printf_fci.param_count = 1;
   printf_fci.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&printf_fci, &printf_fcic TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   printf_fci.params = params_save;
   printf_fci.param_count = param_count_save;
   printf_fci.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// goto L375;
{
goto L375;
phc_check_invariants (TSRMLS_C);
}
// L374:
L374:;
// goto L375;
{
goto L375;
phc_check_invariants (TSRMLS_C);
}
// L375:
L375:;
// goto L376;
{
goto L376;
phc_check_invariants (TSRMLS_C);
}
// L366:
L366:;
// $TLE227 = 1;
{
        if (local_TLE227 == NULL)
    {
      local_TLE227 = EG (uninitialized_zval_ptr);
      local_TLE227->refcount++;
    }
  zval** p_lhs = &local_TLE227;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 1);

phc_check_invariants (TSRMLS_C);
}
// $TLE228 = ($i + $TLE227);
{
    if (local_TLE228 == NULL)
    {
      local_TLE228 = EG (uninitialized_zval_ptr);
      local_TLE228->refcount++;
    }
  zval** p_lhs = &local_TLE228;

    zval* left;
  if (local_i == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_i;

    zval* right;
  if (local_TLE227 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE227;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE229 = 50;
{
        if (local_TLE229 == NULL)
    {
      local_TLE229 = EG (uninitialized_zval_ptr);
      local_TLE229->refcount++;
    }
  zval** p_lhs = &local_TLE229;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 50);

phc_check_invariants (TSRMLS_C);
}
// $TLE230 = ($TLE228 % $TLE229);
{
    if (local_TLE230 == NULL)
    {
      local_TLE230 = EG (uninitialized_zval_ptr);
      local_TLE230->refcount++;
    }
  zval** p_lhs = &local_TLE230;

    zval* left;
  if (local_TLE228 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE228;

    zval* right;
  if (local_TLE229 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE229;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  mod_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE231 = 0;
{
        if (local_TLE231 == NULL)
    {
      local_TLE231 = EG (uninitialized_zval_ptr);
      local_TLE231->refcount++;
    }
  zval** p_lhs = &local_TLE231;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $TLE232 = ($TLE230 != $TLE231);
{
    if (local_TLE232 == NULL)
    {
      local_TLE232 = EG (uninitialized_zval_ptr);
      local_TLE232->refcount++;
    }
  zval** p_lhs = &local_TLE232;

    zval* left;
  if (local_TLE230 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE230;

    zval* right;
  if (local_TLE231 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE231;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_not_equal_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE232) goto L377 else goto L378;
{
     zval* p_cond;
  if (local_TLE232 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE232;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L377;
   else
      goto L378;
phc_check_invariants (TSRMLS_C);
}
// L377:
L377:;
// $TLE233 = '
// ';
{
        if (local_TLE233 == NULL)
    {
      local_TLE233 = EG (uninitialized_zval_ptr);
      local_TLE233->refcount++;
    }
  zval** p_lhs = &local_TLE233;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_STRINGL(value, "\012", 1, 1);

phc_check_invariants (TSRMLS_C);
}
// printf($TLE233);
{
   initialize_function_call (&printf_fci, &printf_fcic, "printf", "tools.source.php", 195 TSRMLS_CC);
      zend_function* signature = printf_fcic.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE233 == NULL)
    {
      local_TLE233 = EG (uninitialized_zval_ptr);
      local_TLE233->refcount++;
    }
  zval** p_arg = &local_TLE233;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE233 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE233;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 195, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = printf_fci.param_count;
   zval*** params_save = printf_fci.params;
   zval** retval_save = printf_fci.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   printf_fci.params = args_ind;
   printf_fci.param_count = 1;
   printf_fci.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&printf_fci, &printf_fcic TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   printf_fci.params = params_save;
   printf_fci.param_count = param_count_save;
   printf_fci.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// goto L379;
{
goto L379;
phc_check_invariants (TSRMLS_C);
}
// L378:
L378:;
// goto L379;
{
goto L379;
phc_check_invariants (TSRMLS_C);
}
// L379:
L379:;
// Method exit
end_of_function:__attribute__((unused));
if (local_ElcfPF2 != NULL)
{
zval_ptr_dtor (&local_ElcfPF2);
}
if (local_TLE218 != NULL)
{
zval_ptr_dtor (&local_TLE218);
}
if (local_TLE219 != NULL)
{
zval_ptr_dtor (&local_TLE219);
}
if (local_TLE220 != NULL)
{
zval_ptr_dtor (&local_TLE220);
}
if (local_TLE221 != NULL)
{
zval_ptr_dtor (&local_TLE221);
}
if (local_TLE222 != NULL)
{
zval_ptr_dtor (&local_TLE222);
}
if (local_TLE223 != NULL)
{
zval_ptr_dtor (&local_TLE223);
}
if (local_TLE224 != NULL)
{
zval_ptr_dtor (&local_TLE224);
}
if (local_TLE225 != NULL)
{
zval_ptr_dtor (&local_TLE225);
}
if (local_TLE226 != NULL)
{
zval_ptr_dtor (&local_TLE226);
}
if (local_TLE227 != NULL)
{
zval_ptr_dtor (&local_TLE227);
}
if (local_TLE228 != NULL)
{
zval_ptr_dtor (&local_TLE228);
}
if (local_TLE229 != NULL)
{
zval_ptr_dtor (&local_TLE229);
}
if (local_TLE230 != NULL)
{
zval_ptr_dtor (&local_TLE230);
}
if (local_TLE231 != NULL)
{
zval_ptr_dtor (&local_TLE231);
}
if (local_TLE232 != NULL)
{
zval_ptr_dtor (&local_TLE232);
}
if (local_TLE233 != NULL)
{
zval_ptr_dtor (&local_TLE233);
}
if (local_TLE322 != NULL)
{
zval_ptr_dtor (&local_TLE322);
}
if (local_TMIi321 != NULL)
{
zval_ptr_dtor (&local_TMIi321);
}
if (local_TMIt320 != NULL)
{
zval_ptr_dtor (&local_TMIt320);
}
if (local_TSt217 != NULL)
{
zval_ptr_dtor (&local_TSt217);
}
if (local_i != NULL)
{
zval_ptr_dtor (&local_i);
}
}
// ArgInfo structures (necessary to support compile time pass-by-reference)
ZEND_BEGIN_ARG_INFO_EX(Memory___construct_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "memorySize")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_addByte_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "byte")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_addString_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "string")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_addShort_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "short")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_addInteger_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "int")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_readByte_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_readShort_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_readInteger_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_resetReadPointer_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_setReadPointer_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_setByte_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "pos")
ZEND_ARG_INFO(0, "byte")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_setShort_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "pos")
ZEND_ARG_INFO(0, "short")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_setInteger_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "pos")
ZEND_ARG_INFO(0, "int")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_getByte_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "pos")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_getShort_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "pos")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_getInteger_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "pos")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_getMemory_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "startPos")
ZEND_ARG_INFO(0, "endPos")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_getMemoryLength_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_setMemorySize_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "size")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_resetMemory_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Memory_dumpMemory_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

static function_entry Memory_functions[] = {
PHP_ME(Memory, __construct, Memory___construct_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, addByte, Memory_addByte_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, addString, Memory_addString_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, addShort, Memory_addShort_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, addInteger, Memory_addInteger_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, readByte, Memory_readByte_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, readShort, Memory_readShort_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, readInteger, Memory_readInteger_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, resetReadPointer, Memory_resetReadPointer_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, setReadPointer, Memory_setReadPointer_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, setByte, Memory_setByte_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, setShort, Memory_setShort_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, setInteger, Memory_setInteger_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, getByte, Memory_getByte_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, getShort, Memory_getShort_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, getInteger, Memory_getInteger_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, getMemory, Memory_getMemory_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, getMemoryLength, Memory_getMemoryLength_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, setMemorySize, Memory_setMemorySize_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, resetMemory, Memory_resetMemory_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(Memory, dumpMemory, Memory_dumpMemory_arg_info, ZEND_ACC_PUBLIC)
{ NULL, NULL, NULL }
};
// class UByte
// {
// 	public $_value = NULL;
// 	public function __construct($value = 0)
// 	{
// 		$TLE234 = 255;
// 		$TLE235 = ($value & $TLE234);
// 		$this->add($TLE235);
// 	}
// 	public function add($value)
// 	{
// 		$TSt236 =& $this->_value;
// 		$TLE237 = 256;
// 		$TLE238 = ($value % $TLE237);
// 		$TSt236 = ($TSt236 + $TLE238);
// 		$TSt239 = $this->_value;
// 		$TLE240 = 255;
// 		$TLE241 = ($TLE240 < $TSt239);
// 		if (TLE241) goto L380 else goto L381;
// 	L380:
// 		$TSt242 = $this->_value;
// 		$TLE243 = 256;
// 		$TLE244 = ($TSt242 % $TLE243);
// 		$this->_value = $TLE244;
// 		goto L382;
// 	L381:
// 		goto L382;
// 	L382:
// 	}
// 	public function subtract($value)
// 	{
// 		$TSt245 =& $this->_value;
// 		$TLE246 = 256;
// 		$TLE247 = ($value % $TLE246);
// 		$TSt245 = ($TSt245 - $TLE247);
// 		$TSt248 = $this->_value;
// 		$TLE249 = 0;
// 		$TLE250 = ($TSt248 < $TLE249);
// 		if (TLE250) goto L383 else goto L384;
// 	L383:
// 		$TSt251 =& $this->_value;
// 		$TLE252 = 255;
// 		$TSt251 = ($TSt251 & $TLE252);
// 		goto L385;
// 	L384:
// 		goto L385;
// 	L385:
// 	}
// 	public function setValue($value)
// 	{
// 		$TLE253 = 255;
// 		$TLE254 = ($value % $TLE253);
// 		$this->_value = $TLE254;
// 	}
// 	public function bitAnd($value)
// 	{
// 		$TSt255 = $this->_value;
// 		$TLE256 = 255;
// 		$TLE257 = ($value & $TLE256);
// 		$TLE258 = ($TSt255 & $TLE257);
// 		$this->_value = $TLE258;
// 	}
// 	public function bitOr($value)
// 	{
// 		$TSt259 = $this->_value;
// 		$TLE260 = 255;
// 		$TLE261 = ($value & $TLE260);
// 		$TLE262 = ($TSt259 | $TLE261);
// 		$this->_value = $TLE262;
// 	}
// 	public function bitXOr($value)
// 	{
// 		$TSt263 = $this->_value;
// 		$TLE264 = 255;
// 		$TLE265 = ($value & $TLE264);
// 		$TLE266 = ($TSt263 ^ $TLE265);
// 		$this->_value = $TLE266;
// 	}
// 	public function bitNot()
// 	{
// 		$TSt267 = $this->_value;
// 		$TLE268 = ~$TSt267;
// 		$TLE269 = 255;
// 		$TLE270 = ($TLE268 & $TLE269);
// 		$this->_value = $TLE270;
// 	}
// 	public function getValue()
// 	{
// 		$TSt271 = $this->_value;
// 		return $TSt271;
// 	}
// 	public function __toString()
// 	{
// 		$TLE272 = $this->getValue();
// 		$TLE273 = (string) $TLE272;
// 		return $TLE273;
// 	}
// }
// public function __construct($value = 0)
// {
// 	$TLE234 = 255;
// 	$TLE235 = ($value & $TLE234);
// 	$this->add($TLE235);
// }
PHP_METHOD(UByte, __construct)
{
zval* local_TLE234 = NULL;
zval* local_TLE235 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
if (num_args <= 0)
{
zval* default_value;
{
zval* local___static_value__ = NULL;
// $__static_value__ = 0;
{
        if (local___static_value__ == NULL)
    {
      local___static_value__ = EG (uninitialized_zval_ptr);
      local___static_value__->refcount++;
    }
  zval** p_lhs = &local___static_value__;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
default_value = local___static_value__;
assert(!default_value->is_ref);
default_value->refcount++;
if (local___static_value__ != NULL)
{
zval_ptr_dtor (&local___static_value__);
}
}
default_value->refcount--;
	params[0] = default_value;
}
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TLE234 = 255;
{
        if (local_TLE234 == NULL)
    {
      local_TLE234 = EG (uninitialized_zval_ptr);
      local_TLE234->refcount++;
    }
  zval** p_lhs = &local_TLE234;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE235 = ($value & $TLE234);
{
    if (local_TLE235 == NULL)
    {
      local_TLE235 = EG (uninitialized_zval_ptr);
      local_TLE235->refcount++;
    }
  zval** p_lhs = &local_TLE235;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE234 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE234;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->add($TLE235);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "add", "tools.source.php", 222 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE235 == NULL)
    {
      local_TLE235 = EG (uninitialized_zval_ptr);
      local_TLE235->refcount++;
    }
  zval** p_arg = &local_TLE235;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE235 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE235;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 222, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE234 != NULL)
{
zval_ptr_dtor (&local_TLE234);
}
if (local_TLE235 != NULL)
{
zval_ptr_dtor (&local_TLE235);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function add($value)
// {
// 	$TSt236 =& $this->_value;
// 	$TLE237 = 256;
// 	$TLE238 = ($value % $TLE237);
// 	$TSt236 = ($TSt236 + $TLE238);
// 	$TSt239 = $this->_value;
// 	$TLE240 = 255;
// 	$TLE241 = ($TLE240 < $TSt239);
// 	if (TLE241) goto L380 else goto L381;
// L380:
// 	$TSt242 = $this->_value;
// 	$TLE243 = 256;
// 	$TLE244 = ($TSt242 % $TLE243);
// 	$this->_value = $TLE244;
// 	goto L382;
// L381:
// 	goto L382;
// L382:
// }
PHP_METHOD(UByte, add)
{
zval* local_TLE237 = NULL;
zval* local_TLE238 = NULL;
zval* local_TLE240 = NULL;
zval* local_TLE241 = NULL;
zval* local_TLE243 = NULL;
zval* local_TLE244 = NULL;
zval* local_TSt236 = NULL;
zval* local_TSt239 = NULL;
zval* local_TSt242 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TSt236 =& $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TSt236 == NULL)
    {
      local_TSt236 = EG (uninitialized_zval_ptr);
      local_TSt236->refcount++;
    }
  zval** p_lhs = &local_TSt236;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// $TLE237 = 256;
{
        if (local_TLE237 == NULL)
    {
      local_TLE237 = EG (uninitialized_zval_ptr);
      local_TLE237->refcount++;
    }
  zval** p_lhs = &local_TLE237;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 256);

phc_check_invariants (TSRMLS_C);
}
// $TLE238 = ($value % $TLE237);
{
    if (local_TLE238 == NULL)
    {
      local_TLE238 = EG (uninitialized_zval_ptr);
      local_TLE238->refcount++;
    }
  zval** p_lhs = &local_TLE238;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE237 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE237;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  mod_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TSt236 = ($TSt236 + $TLE238);
{
    if (local_TSt236 == NULL)
    {
      local_TSt236 = EG (uninitialized_zval_ptr);
      local_TSt236->refcount++;
    }
  zval** p_lhs = &local_TSt236;

    zval* left;
  if (local_TSt236 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt236;

    zval* right;
  if (local_TLE238 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE238;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TSt239 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt239 == NULL)
    {
      local_TSt239 = EG (uninitialized_zval_ptr);
      local_TSt239->refcount++;
    }
  zval** p_lhs = &local_TSt239;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE240 = 255;
{
        if (local_TLE240 == NULL)
    {
      local_TLE240 = EG (uninitialized_zval_ptr);
      local_TLE240->refcount++;
    }
  zval** p_lhs = &local_TLE240;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE241 = ($TLE240 < $TSt239);
{
    if (local_TLE241 == NULL)
    {
      local_TLE241 = EG (uninitialized_zval_ptr);
      local_TLE241->refcount++;
    }
  zval** p_lhs = &local_TLE241;

    zval* left;
  if (local_TLE240 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE240;

    zval* right;
  if (local_TSt239 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TSt239;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_smaller_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE241) goto L380 else goto L381;
{
     zval* p_cond;
  if (local_TLE241 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE241;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L380;
   else
      goto L381;
phc_check_invariants (TSRMLS_C);
}
// L380:
L380:;
// $TSt242 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt242 == NULL)
    {
      local_TSt242 = EG (uninitialized_zval_ptr);
      local_TSt242->refcount++;
    }
  zval** p_lhs = &local_TSt242;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE243 = 256;
{
        if (local_TLE243 == NULL)
    {
      local_TLE243 = EG (uninitialized_zval_ptr);
      local_TLE243->refcount++;
    }
  zval** p_lhs = &local_TLE243;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 256);

phc_check_invariants (TSRMLS_C);
}
// $TLE244 = ($TSt242 % $TLE243);
{
    if (local_TLE244 == NULL)
    {
      local_TLE244 = EG (uninitialized_zval_ptr);
      local_TLE244->refcount++;
    }
  zval** p_lhs = &local_TLE244;

    zval* left;
  if (local_TSt242 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt242;

    zval* right;
  if (local_TLE243 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE243;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  mod_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->_value = $TLE244;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE244 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE244;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// goto L382;
{
goto L382;
phc_check_invariants (TSRMLS_C);
}
// L381:
L381:;
// goto L382;
{
goto L382;
phc_check_invariants (TSRMLS_C);
}
// L382:
L382:;
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE237 != NULL)
{
zval_ptr_dtor (&local_TLE237);
}
if (local_TLE238 != NULL)
{
zval_ptr_dtor (&local_TLE238);
}
if (local_TLE240 != NULL)
{
zval_ptr_dtor (&local_TLE240);
}
if (local_TLE241 != NULL)
{
zval_ptr_dtor (&local_TLE241);
}
if (local_TLE243 != NULL)
{
zval_ptr_dtor (&local_TLE243);
}
if (local_TLE244 != NULL)
{
zval_ptr_dtor (&local_TLE244);
}
if (local_TSt236 != NULL)
{
zval_ptr_dtor (&local_TSt236);
}
if (local_TSt239 != NULL)
{
zval_ptr_dtor (&local_TSt239);
}
if (local_TSt242 != NULL)
{
zval_ptr_dtor (&local_TSt242);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function subtract($value)
// {
// 	$TSt245 =& $this->_value;
// 	$TLE246 = 256;
// 	$TLE247 = ($value % $TLE246);
// 	$TSt245 = ($TSt245 - $TLE247);
// 	$TSt248 = $this->_value;
// 	$TLE249 = 0;
// 	$TLE250 = ($TSt248 < $TLE249);
// 	if (TLE250) goto L383 else goto L384;
// L383:
// 	$TSt251 =& $this->_value;
// 	$TLE252 = 255;
// 	$TSt251 = ($TSt251 & $TLE252);
// 	goto L385;
// L384:
// 	goto L385;
// L385:
// }
PHP_METHOD(UByte, subtract)
{
zval* local_TLE246 = NULL;
zval* local_TLE247 = NULL;
zval* local_TLE249 = NULL;
zval* local_TLE250 = NULL;
zval* local_TLE252 = NULL;
zval* local_TSt245 = NULL;
zval* local_TSt248 = NULL;
zval* local_TSt251 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TSt245 =& $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TSt245 == NULL)
    {
      local_TSt245 = EG (uninitialized_zval_ptr);
      local_TSt245->refcount++;
    }
  zval** p_lhs = &local_TSt245;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// $TLE246 = 256;
{
        if (local_TLE246 == NULL)
    {
      local_TLE246 = EG (uninitialized_zval_ptr);
      local_TLE246->refcount++;
    }
  zval** p_lhs = &local_TLE246;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 256);

phc_check_invariants (TSRMLS_C);
}
// $TLE247 = ($value % $TLE246);
{
    if (local_TLE247 == NULL)
    {
      local_TLE247 = EG (uninitialized_zval_ptr);
      local_TLE247->refcount++;
    }
  zval** p_lhs = &local_TLE247;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE246 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE246;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  mod_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TSt245 = ($TSt245 - $TLE247);
{
    if (local_TSt245 == NULL)
    {
      local_TSt245 = EG (uninitialized_zval_ptr);
      local_TSt245->refcount++;
    }
  zval** p_lhs = &local_TSt245;

    zval* left;
  if (local_TSt245 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt245;

    zval* right;
  if (local_TLE247 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE247;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TSt248 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt248 == NULL)
    {
      local_TSt248 = EG (uninitialized_zval_ptr);
      local_TSt248->refcount++;
    }
  zval** p_lhs = &local_TSt248;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE249 = 0;
{
        if (local_TLE249 == NULL)
    {
      local_TLE249 = EG (uninitialized_zval_ptr);
      local_TLE249->refcount++;
    }
  zval** p_lhs = &local_TLE249;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $TLE250 = ($TSt248 < $TLE249);
{
    if (local_TLE250 == NULL)
    {
      local_TLE250 = EG (uninitialized_zval_ptr);
      local_TLE250->refcount++;
    }
  zval** p_lhs = &local_TLE250;

    zval* left;
  if (local_TSt248 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt248;

    zval* right;
  if (local_TLE249 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE249;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_smaller_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE250) goto L383 else goto L384;
{
     zval* p_cond;
  if (local_TLE250 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE250;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L383;
   else
      goto L384;
phc_check_invariants (TSRMLS_C);
}
// L383:
L383:;
// $TSt251 =& $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TSt251 == NULL)
    {
      local_TSt251 = EG (uninitialized_zval_ptr);
      local_TSt251->refcount++;
    }
  zval** p_lhs = &local_TSt251;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// $TLE252 = 255;
{
        if (local_TLE252 == NULL)
    {
      local_TLE252 = EG (uninitialized_zval_ptr);
      local_TLE252->refcount++;
    }
  zval** p_lhs = &local_TLE252;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TSt251 = ($TSt251 & $TLE252);
{
    if (local_TSt251 == NULL)
    {
      local_TSt251 = EG (uninitialized_zval_ptr);
      local_TSt251->refcount++;
    }
  zval** p_lhs = &local_TSt251;

    zval* left;
  if (local_TSt251 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt251;

    zval* right;
  if (local_TLE252 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE252;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// goto L385;
{
goto L385;
phc_check_invariants (TSRMLS_C);
}
// L384:
L384:;
// goto L385;
{
goto L385;
phc_check_invariants (TSRMLS_C);
}
// L385:
L385:;
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE246 != NULL)
{
zval_ptr_dtor (&local_TLE246);
}
if (local_TLE247 != NULL)
{
zval_ptr_dtor (&local_TLE247);
}
if (local_TLE249 != NULL)
{
zval_ptr_dtor (&local_TLE249);
}
if (local_TLE250 != NULL)
{
zval_ptr_dtor (&local_TLE250);
}
if (local_TLE252 != NULL)
{
zval_ptr_dtor (&local_TLE252);
}
if (local_TSt245 != NULL)
{
zval_ptr_dtor (&local_TSt245);
}
if (local_TSt248 != NULL)
{
zval_ptr_dtor (&local_TSt248);
}
if (local_TSt251 != NULL)
{
zval_ptr_dtor (&local_TSt251);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function setValue($value)
// {
// 	$TLE253 = 255;
// 	$TLE254 = ($value % $TLE253);
// 	$this->_value = $TLE254;
// }
PHP_METHOD(UByte, setValue)
{
zval* local_TLE253 = NULL;
zval* local_TLE254 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TLE253 = 255;
{
        if (local_TLE253 == NULL)
    {
      local_TLE253 = EG (uninitialized_zval_ptr);
      local_TLE253->refcount++;
    }
  zval** p_lhs = &local_TLE253;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE254 = ($value % $TLE253);
{
    if (local_TLE254 == NULL)
    {
      local_TLE254 = EG (uninitialized_zval_ptr);
      local_TLE254->refcount++;
    }
  zval** p_lhs = &local_TLE254;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE253 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE253;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  mod_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->_value = $TLE254;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE254 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE254;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE253 != NULL)
{
zval_ptr_dtor (&local_TLE253);
}
if (local_TLE254 != NULL)
{
zval_ptr_dtor (&local_TLE254);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function bitAnd($value)
// {
// 	$TSt255 = $this->_value;
// 	$TLE256 = 255;
// 	$TLE257 = ($value & $TLE256);
// 	$TLE258 = ($TSt255 & $TLE257);
// 	$this->_value = $TLE258;
// }
PHP_METHOD(UByte, bitAnd)
{
zval* local_TLE256 = NULL;
zval* local_TLE257 = NULL;
zval* local_TLE258 = NULL;
zval* local_TSt255 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TSt255 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt255 == NULL)
    {
      local_TSt255 = EG (uninitialized_zval_ptr);
      local_TSt255->refcount++;
    }
  zval** p_lhs = &local_TSt255;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE256 = 255;
{
        if (local_TLE256 == NULL)
    {
      local_TLE256 = EG (uninitialized_zval_ptr);
      local_TLE256->refcount++;
    }
  zval** p_lhs = &local_TLE256;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE257 = ($value & $TLE256);
{
    if (local_TLE257 == NULL)
    {
      local_TLE257 = EG (uninitialized_zval_ptr);
      local_TLE257->refcount++;
    }
  zval** p_lhs = &local_TLE257;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE256 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE256;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE258 = ($TSt255 & $TLE257);
{
    if (local_TLE258 == NULL)
    {
      local_TLE258 = EG (uninitialized_zval_ptr);
      local_TLE258->refcount++;
    }
  zval** p_lhs = &local_TLE258;

    zval* left;
  if (local_TSt255 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt255;

    zval* right;
  if (local_TLE257 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE257;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->_value = $TLE258;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE258 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE258;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE256 != NULL)
{
zval_ptr_dtor (&local_TLE256);
}
if (local_TLE257 != NULL)
{
zval_ptr_dtor (&local_TLE257);
}
if (local_TLE258 != NULL)
{
zval_ptr_dtor (&local_TLE258);
}
if (local_TSt255 != NULL)
{
zval_ptr_dtor (&local_TSt255);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function bitOr($value)
// {
// 	$TSt259 = $this->_value;
// 	$TLE260 = 255;
// 	$TLE261 = ($value & $TLE260);
// 	$TLE262 = ($TSt259 | $TLE261);
// 	$this->_value = $TLE262;
// }
PHP_METHOD(UByte, bitOr)
{
zval* local_TLE260 = NULL;
zval* local_TLE261 = NULL;
zval* local_TLE262 = NULL;
zval* local_TSt259 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TSt259 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt259 == NULL)
    {
      local_TSt259 = EG (uninitialized_zval_ptr);
      local_TSt259->refcount++;
    }
  zval** p_lhs = &local_TSt259;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE260 = 255;
{
        if (local_TLE260 == NULL)
    {
      local_TLE260 = EG (uninitialized_zval_ptr);
      local_TLE260->refcount++;
    }
  zval** p_lhs = &local_TLE260;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE261 = ($value & $TLE260);
{
    if (local_TLE261 == NULL)
    {
      local_TLE261 = EG (uninitialized_zval_ptr);
      local_TLE261->refcount++;
    }
  zval** p_lhs = &local_TLE261;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE260 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE260;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE262 = ($TSt259 | $TLE261);
{
    if (local_TLE262 == NULL)
    {
      local_TLE262 = EG (uninitialized_zval_ptr);
      local_TLE262->refcount++;
    }
  zval** p_lhs = &local_TLE262;

    zval* left;
  if (local_TSt259 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt259;

    zval* right;
  if (local_TLE261 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE261;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_or_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->_value = $TLE262;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE262 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE262;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE260 != NULL)
{
zval_ptr_dtor (&local_TLE260);
}
if (local_TLE261 != NULL)
{
zval_ptr_dtor (&local_TLE261);
}
if (local_TLE262 != NULL)
{
zval_ptr_dtor (&local_TLE262);
}
if (local_TSt259 != NULL)
{
zval_ptr_dtor (&local_TSt259);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function bitXOr($value)
// {
// 	$TSt263 = $this->_value;
// 	$TLE264 = 255;
// 	$TLE265 = ($value & $TLE264);
// 	$TLE266 = ($TSt263 ^ $TLE265);
// 	$this->_value = $TLE266;
// }
PHP_METHOD(UByte, bitXOr)
{
zval* local_TLE264 = NULL;
zval* local_TLE265 = NULL;
zval* local_TLE266 = NULL;
zval* local_TSt263 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TSt263 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt263 == NULL)
    {
      local_TSt263 = EG (uninitialized_zval_ptr);
      local_TSt263->refcount++;
    }
  zval** p_lhs = &local_TSt263;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE264 = 255;
{
        if (local_TLE264 == NULL)
    {
      local_TLE264 = EG (uninitialized_zval_ptr);
      local_TLE264->refcount++;
    }
  zval** p_lhs = &local_TLE264;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE265 = ($value & $TLE264);
{
    if (local_TLE265 == NULL)
    {
      local_TLE265 = EG (uninitialized_zval_ptr);
      local_TLE265->refcount++;
    }
  zval** p_lhs = &local_TLE265;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE264 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE264;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE266 = ($TSt263 ^ $TLE265);
{
    if (local_TLE266 == NULL)
    {
      local_TLE266 = EG (uninitialized_zval_ptr);
      local_TLE266->refcount++;
    }
  zval** p_lhs = &local_TLE266;

    zval* left;
  if (local_TSt263 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt263;

    zval* right;
  if (local_TLE265 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE265;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_xor_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->_value = $TLE266;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE266 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE266;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE264 != NULL)
{
zval_ptr_dtor (&local_TLE264);
}
if (local_TLE265 != NULL)
{
zval_ptr_dtor (&local_TLE265);
}
if (local_TLE266 != NULL)
{
zval_ptr_dtor (&local_TLE266);
}
if (local_TSt263 != NULL)
{
zval_ptr_dtor (&local_TSt263);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function bitNot()
// {
// 	$TSt267 = $this->_value;
// 	$TLE268 = ~$TSt267;
// 	$TLE269 = 255;
// 	$TLE270 = ($TLE268 & $TLE269);
// 	$this->_value = $TLE270;
// }
PHP_METHOD(UByte, bitNot)
{
zval* local_TLE268 = NULL;
zval* local_TLE269 = NULL;
zval* local_TLE270 = NULL;
zval* local_TSt267 = NULL;
zval* local_this = getThis();
// Function body
// $TSt267 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt267 == NULL)
    {
      local_TSt267 = EG (uninitialized_zval_ptr);
      local_TSt267->refcount++;
    }
  zval** p_lhs = &local_TSt267;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE268 = ~$TSt267;
{
     if (local_TLE268 == NULL)
    {
      local_TLE268 = EG (uninitialized_zval_ptr);
      local_TLE268->refcount++;
    }
  zval** p_lhs = &local_TLE268;

     zval* rhs;
  if (local_TSt267 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TSt267;

   if (in_copy_on_write (*p_lhs))
   {
     zval_ptr_dtor (p_lhs);
     ALLOC_INIT_ZVAL (*p_lhs);
   }

   zval old = **p_lhs;
   int result_is_operand = (*p_lhs == rhs);
   bitwise_not_function (*p_lhs, rhs TSRMLS_CC);
   if (!result_is_operand)
	zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE269 = 255;
{
        if (local_TLE269 == NULL)
    {
      local_TLE269 = EG (uninitialized_zval_ptr);
      local_TLE269->refcount++;
    }
  zval** p_lhs = &local_TLE269;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 255);

phc_check_invariants (TSRMLS_C);
}
// $TLE270 = ($TLE268 & $TLE269);
{
    if (local_TLE270 == NULL)
    {
      local_TLE270 = EG (uninitialized_zval_ptr);
      local_TLE270->refcount++;
    }
  zval** p_lhs = &local_TLE270;

    zval* left;
  if (local_TLE268 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE268;

    zval* right;
  if (local_TLE269 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE269;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->_value = $TLE270;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE270 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE270;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE268 != NULL)
{
zval_ptr_dtor (&local_TLE268);
}
if (local_TLE269 != NULL)
{
zval_ptr_dtor (&local_TLE269);
}
if (local_TLE270 != NULL)
{
zval_ptr_dtor (&local_TLE270);
}
if (local_TSt267 != NULL)
{
zval_ptr_dtor (&local_TSt267);
}
}
// public function getValue()
// {
// 	$TSt271 = $this->_value;
// 	return $TSt271;
// }
PHP_METHOD(UByte, getValue)
{
zval* local_TSt271 = NULL;
zval* local_this = getThis();
// Function body
// $TSt271 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt271 == NULL)
    {
      local_TSt271 = EG (uninitialized_zval_ptr);
      local_TSt271->refcount++;
    }
  zval** p_lhs = &local_TSt271;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// return $TSt271;
{
     zval* rhs;
  if (local_TSt271 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TSt271;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TSt271 != NULL)
{
zval_ptr_dtor (&local_TSt271);
}
}
// public function __toString()
// {
// 	$TLE272 = $this->getValue();
// 	$TLE273 = (string) $TLE272;
// 	return $TLE273;
// }
PHP_METHOD(UByte, __toString)
{
zval* local_TLE272 = NULL;
zval* local_TLE273 = NULL;
zval* local_this = getThis();
// Function body
// $TLE272 = $this->getValue();
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "getValue", "tools.source.php", 254 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[0];
   int abr_index = 0;
   

   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [0];
   zval* args [0];
   zval** args_ind [0];

   int af_index = 0;
   

   phc_setup_error (1, "tools.source.php", 254, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 0;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 0; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE272 == NULL)
    {
      local_TLE272 = EG (uninitialized_zval_ptr);
      local_TLE272->refcount++;
    }
  zval** p_lhs = &local_TLE272;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE273 = (string) $TLE272;
{
      if (local_TLE273 == NULL)
    {
      local_TLE273 = EG (uninitialized_zval_ptr);
      local_TLE273->refcount++;
    }
  zval** p_lhs = &local_TLE273;

    zval* rhs;
  if (local_TLE272 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE272;

  if (*p_lhs != rhs)
    {
        if ((*p_lhs)->is_ref)
      overwrite_lhs (*p_lhs, rhs);
  else
    {
      zval_ptr_dtor (p_lhs);
        if (rhs->is_ref)
    {
      // Take a copy of RHS for LHS
      *p_lhs = zvp_clone_ex (rhs);
    }
  else
    {
      // Share a copy
      rhs->refcount++;
      *p_lhs = rhs;
    }

    }

    }

    assert (IS_STRING >= 0 && IS_STRING <= 6);
  if ((*p_lhs)->type != IS_STRING)
  {
    sep_copy_on_write (p_lhs);
    convert_to_string (*p_lhs);
  }

phc_check_invariants (TSRMLS_C);
}
// return $TLE273;
{
     zval* rhs;
  if (local_TLE273 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE273;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE272 != NULL)
{
zval_ptr_dtor (&local_TLE272);
}
if (local_TLE273 != NULL)
{
zval_ptr_dtor (&local_TLE273);
}
}
// ArgInfo structures (necessary to support compile time pass-by-reference)
ZEND_BEGIN_ARG_INFO_EX(UByte___construct_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UByte_add_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UByte_subtract_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UByte_setValue_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UByte_bitAnd_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UByte_bitOr_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UByte_bitXOr_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UByte_bitNot_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UByte_getValue_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UByte___toString_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

static function_entry UByte_functions[] = {
PHP_ME(UByte, __construct, UByte___construct_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UByte, add, UByte_add_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UByte, subtract, UByte_subtract_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UByte, setValue, UByte_setValue_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UByte, bitAnd, UByte_bitAnd_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UByte, bitOr, UByte_bitOr_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UByte, bitXOr, UByte_bitXOr_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UByte, bitNot, UByte_bitNot_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UByte, getValue, UByte_getValue_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UByte, __toString, UByte___toString_arg_info, ZEND_ACC_PUBLIC)
{ NULL, NULL, NULL }
};
// class UShort
// {
// 	public $_value = NULL;
// 	public function __construct($value = 0)
// 	{
// 		$TLE274 = 65535;
// 		$TLE275 = ($value & $TLE274);
// 		$this->add($TLE275);
// 	}
// 	public function add($value)
// 	{
// 		$TSt276 =& $this->_value;
// 		$TLE277 = 65536;
// 		$TLE278 = ($value % $TLE277);
// 		$TSt276 = ($TSt276 + $TLE278);
// 		$TSt279 = $this->_value;
// 		$TLE280 = 65535;
// 		$TLE281 = ($TLE280 < $TSt279);
// 		if (TLE281) goto L386 else goto L387;
// 	L386:
// 		$TSt282 = $this->_value;
// 		$TLE283 = 65535;
// 		$TLE284 = ($TSt282 % $TLE283);
// 		$this->_value = $TLE284;
// 		goto L388;
// 	L387:
// 		goto L388;
// 	L388:
// 	}
// 	public function subtract($value)
// 	{
// 		$TSt285 =& $this->_value;
// 		$TLE286 = 65536;
// 		$TLE287 = ($value % $TLE286);
// 		$TSt285 = ($TSt285 - $TLE287);
// 		$TSt288 = $this->_value;
// 		$TLE289 = 0;
// 		$TLE290 = ($TSt288 < $TLE289);
// 		if (TLE290) goto L389 else goto L390;
// 	L389:
// 		$TSt291 =& $this->_value;
// 		$TLE292 = 65535;
// 		$TSt291 = ($TSt291 & $TLE292);
// 		goto L391;
// 	L390:
// 		goto L391;
// 	L391:
// 	}
// 	public function setValue($value)
// 	{
// 		$TLE293 = 65535;
// 		$TLE294 = ($value % $TLE293);
// 		$this->_value = $TLE294;
// 	}
// 	public function bitAnd($value)
// 	{
// 		$TSt295 = $this->_value;
// 		$TLE296 = 65535;
// 		$TLE297 = ($value & $TLE296);
// 		$TLE298 = ($TSt295 & $TLE297);
// 		$this->_value = $TLE298;
// 	}
// 	public function bitOr($value)
// 	{
// 		$TSt299 = $this->_value;
// 		$TLE300 = 65535;
// 		$TLE301 = ($value & $TLE300);
// 		$TLE302 = ($TSt299 | $TLE301);
// 		$this->_value = $TLE302;
// 	}
// 	public function bitXOr($value)
// 	{
// 		$TSt303 = $this->_value;
// 		$TLE304 = 65535;
// 		$TLE305 = ($value & $TLE304);
// 		$TLE306 = ($TSt303 ^ $TLE305);
// 		$this->_value = $TLE306;
// 	}
// 	public function bitNot()
// 	{
// 		$TSt307 = $this->_value;
// 		$TLE308 = ~$TSt307;
// 		$TLE309 = 65535;
// 		$TLE310 = ($TLE308 & $TLE309);
// 		$this->_value = $TLE310;
// 	}
// 	public function getValue()
// 	{
// 		$TSt311 = $this->_value;
// 		return $TSt311;
// 	}
// 	public function __toString()
// 	{
// 		$TLE312 = $this->getValue();
// 		$TLE313 = (string) $TLE312;
// 		return $TLE313;
// 	}
// }
// public function __construct($value = 0)
// {
// 	$TLE274 = 65535;
// 	$TLE275 = ($value & $TLE274);
// 	$this->add($TLE275);
// }
PHP_METHOD(UShort, __construct)
{
zval* local_TLE274 = NULL;
zval* local_TLE275 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
if (num_args <= 0)
{
zval* default_value;
{
zval* local___static_value__ = NULL;
// $__static_value__ = 0;
{
        if (local___static_value__ == NULL)
    {
      local___static_value__ = EG (uninitialized_zval_ptr);
      local___static_value__->refcount++;
    }
  zval** p_lhs = &local___static_value__;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
default_value = local___static_value__;
assert(!default_value->is_ref);
default_value->refcount++;
if (local___static_value__ != NULL)
{
zval_ptr_dtor (&local___static_value__);
}
}
default_value->refcount--;
	params[0] = default_value;
}
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TLE274 = 65535;
{
        if (local_TLE274 == NULL)
    {
      local_TLE274 = EG (uninitialized_zval_ptr);
      local_TLE274->refcount++;
    }
  zval** p_lhs = &local_TLE274;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65535);

phc_check_invariants (TSRMLS_C);
}
// $TLE275 = ($value & $TLE274);
{
    if (local_TLE275 == NULL)
    {
      local_TLE275 = EG (uninitialized_zval_ptr);
      local_TLE275->refcount++;
    }
  zval** p_lhs = &local_TLE275;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE274 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE274;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->add($TLE275);
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "add", "tools.source.php", 281 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[1];
   int abr_index = 0;
      // TODO: find names to replace index
   if (arg_info)
   {
      by_ref[abr_index] = arg_info->pass_by_reference;
      arg_info++;
   }
   else
      by_ref[abr_index] = signature->common.pass_rest_by_reference;

   abr_index++;


   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [1];
   zval* args [1];
   zval** args_ind [1];

   int af_index = 0;
      destruct[af_index] = 0;
   if (by_ref[af_index])
   {
        if (local_TLE275 == NULL)
    {
      local_TLE275 = EG (uninitialized_zval_ptr);
      local_TLE275->refcount++;
    }
  zval** p_arg = &local_TLE275;

      args_ind[af_index] = fetch_var_arg_by_ref (p_arg);
      assert (!in_copy_on_write (*args_ind[af_index]));
      args[af_index] = *args_ind[af_index];
   }
   else
   {
        zval* arg;
  if (local_TLE275 == NULL)
    arg = EG (uninitialized_zval_ptr);
  else
    arg = local_TLE275;

      args[af_index] = fetch_var_arg (arg, &destruct[af_index]);
      args_ind[af_index] = &args[af_index];
   }
   af_index++;


   phc_setup_error (1, "tools.source.php", 281, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 1;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 1; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

   

   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE274 != NULL)
{
zval_ptr_dtor (&local_TLE274);
}
if (local_TLE275 != NULL)
{
zval_ptr_dtor (&local_TLE275);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function add($value)
// {
// 	$TSt276 =& $this->_value;
// 	$TLE277 = 65536;
// 	$TLE278 = ($value % $TLE277);
// 	$TSt276 = ($TSt276 + $TLE278);
// 	$TSt279 = $this->_value;
// 	$TLE280 = 65535;
// 	$TLE281 = ($TLE280 < $TSt279);
// 	if (TLE281) goto L386 else goto L387;
// L386:
// 	$TSt282 = $this->_value;
// 	$TLE283 = 65535;
// 	$TLE284 = ($TSt282 % $TLE283);
// 	$this->_value = $TLE284;
// 	goto L388;
// L387:
// 	goto L388;
// L388:
// }
PHP_METHOD(UShort, add)
{
zval* local_TLE277 = NULL;
zval* local_TLE278 = NULL;
zval* local_TLE280 = NULL;
zval* local_TLE281 = NULL;
zval* local_TLE283 = NULL;
zval* local_TLE284 = NULL;
zval* local_TSt276 = NULL;
zval* local_TSt279 = NULL;
zval* local_TSt282 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TSt276 =& $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TSt276 == NULL)
    {
      local_TSt276 = EG (uninitialized_zval_ptr);
      local_TSt276->refcount++;
    }
  zval** p_lhs = &local_TSt276;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// $TLE277 = 65536;
{
        if (local_TLE277 == NULL)
    {
      local_TLE277 = EG (uninitialized_zval_ptr);
      local_TLE277->refcount++;
    }
  zval** p_lhs = &local_TLE277;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65536);

phc_check_invariants (TSRMLS_C);
}
// $TLE278 = ($value % $TLE277);
{
    if (local_TLE278 == NULL)
    {
      local_TLE278 = EG (uninitialized_zval_ptr);
      local_TLE278->refcount++;
    }
  zval** p_lhs = &local_TLE278;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE277 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE277;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  mod_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TSt276 = ($TSt276 + $TLE278);
{
    if (local_TSt276 == NULL)
    {
      local_TSt276 = EG (uninitialized_zval_ptr);
      local_TSt276->refcount++;
    }
  zval** p_lhs = &local_TSt276;

    zval* left;
  if (local_TSt276 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt276;

    zval* right;
  if (local_TLE278 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE278;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  add_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TSt279 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt279 == NULL)
    {
      local_TSt279 = EG (uninitialized_zval_ptr);
      local_TSt279->refcount++;
    }
  zval** p_lhs = &local_TSt279;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE280 = 65535;
{
        if (local_TLE280 == NULL)
    {
      local_TLE280 = EG (uninitialized_zval_ptr);
      local_TLE280->refcount++;
    }
  zval** p_lhs = &local_TLE280;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65535);

phc_check_invariants (TSRMLS_C);
}
// $TLE281 = ($TLE280 < $TSt279);
{
    if (local_TLE281 == NULL)
    {
      local_TLE281 = EG (uninitialized_zval_ptr);
      local_TLE281->refcount++;
    }
  zval** p_lhs = &local_TLE281;

    zval* left;
  if (local_TLE280 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE280;

    zval* right;
  if (local_TSt279 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TSt279;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_smaller_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE281) goto L386 else goto L387;
{
     zval* p_cond;
  if (local_TLE281 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE281;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L386;
   else
      goto L387;
phc_check_invariants (TSRMLS_C);
}
// L386:
L386:;
// $TSt282 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt282 == NULL)
    {
      local_TSt282 = EG (uninitialized_zval_ptr);
      local_TSt282->refcount++;
    }
  zval** p_lhs = &local_TSt282;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE283 = 65535;
{
        if (local_TLE283 == NULL)
    {
      local_TLE283 = EG (uninitialized_zval_ptr);
      local_TLE283->refcount++;
    }
  zval** p_lhs = &local_TLE283;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65535);

phc_check_invariants (TSRMLS_C);
}
// $TLE284 = ($TSt282 % $TLE283);
{
    if (local_TLE284 == NULL)
    {
      local_TLE284 = EG (uninitialized_zval_ptr);
      local_TLE284->refcount++;
    }
  zval** p_lhs = &local_TLE284;

    zval* left;
  if (local_TSt282 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt282;

    zval* right;
  if (local_TLE283 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE283;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  mod_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->_value = $TLE284;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE284 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE284;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// goto L388;
{
goto L388;
phc_check_invariants (TSRMLS_C);
}
// L387:
L387:;
// goto L388;
{
goto L388;
phc_check_invariants (TSRMLS_C);
}
// L388:
L388:;
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE277 != NULL)
{
zval_ptr_dtor (&local_TLE277);
}
if (local_TLE278 != NULL)
{
zval_ptr_dtor (&local_TLE278);
}
if (local_TLE280 != NULL)
{
zval_ptr_dtor (&local_TLE280);
}
if (local_TLE281 != NULL)
{
zval_ptr_dtor (&local_TLE281);
}
if (local_TLE283 != NULL)
{
zval_ptr_dtor (&local_TLE283);
}
if (local_TLE284 != NULL)
{
zval_ptr_dtor (&local_TLE284);
}
if (local_TSt276 != NULL)
{
zval_ptr_dtor (&local_TSt276);
}
if (local_TSt279 != NULL)
{
zval_ptr_dtor (&local_TSt279);
}
if (local_TSt282 != NULL)
{
zval_ptr_dtor (&local_TSt282);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function subtract($value)
// {
// 	$TSt285 =& $this->_value;
// 	$TLE286 = 65536;
// 	$TLE287 = ($value % $TLE286);
// 	$TSt285 = ($TSt285 - $TLE287);
// 	$TSt288 = $this->_value;
// 	$TLE289 = 0;
// 	$TLE290 = ($TSt288 < $TLE289);
// 	if (TLE290) goto L389 else goto L390;
// L389:
// 	$TSt291 =& $this->_value;
// 	$TLE292 = 65535;
// 	$TSt291 = ($TSt291 & $TLE292);
// 	goto L391;
// L390:
// 	goto L391;
// L391:
// }
PHP_METHOD(UShort, subtract)
{
zval* local_TLE286 = NULL;
zval* local_TLE287 = NULL;
zval* local_TLE289 = NULL;
zval* local_TLE290 = NULL;
zval* local_TLE292 = NULL;
zval* local_TSt285 = NULL;
zval* local_TSt288 = NULL;
zval* local_TSt291 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TSt285 =& $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TSt285 == NULL)
    {
      local_TSt285 = EG (uninitialized_zval_ptr);
      local_TSt285->refcount++;
    }
  zval** p_lhs = &local_TSt285;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// $TLE286 = 65536;
{
        if (local_TLE286 == NULL)
    {
      local_TLE286 = EG (uninitialized_zval_ptr);
      local_TLE286->refcount++;
    }
  zval** p_lhs = &local_TLE286;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65536);

phc_check_invariants (TSRMLS_C);
}
// $TLE287 = ($value % $TLE286);
{
    if (local_TLE287 == NULL)
    {
      local_TLE287 = EG (uninitialized_zval_ptr);
      local_TLE287->refcount++;
    }
  zval** p_lhs = &local_TLE287;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE286 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE286;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  mod_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TSt285 = ($TSt285 - $TLE287);
{
    if (local_TSt285 == NULL)
    {
      local_TSt285 = EG (uninitialized_zval_ptr);
      local_TSt285->refcount++;
    }
  zval** p_lhs = &local_TSt285;

    zval* left;
  if (local_TSt285 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt285;

    zval* right;
  if (local_TLE287 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE287;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  sub_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TSt288 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt288 == NULL)
    {
      local_TSt288 = EG (uninitialized_zval_ptr);
      local_TSt288->refcount++;
    }
  zval** p_lhs = &local_TSt288;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE289 = 0;
{
        if (local_TLE289 == NULL)
    {
      local_TLE289 = EG (uninitialized_zval_ptr);
      local_TLE289->refcount++;
    }
  zval** p_lhs = &local_TLE289;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 0);

phc_check_invariants (TSRMLS_C);
}
// $TLE290 = ($TSt288 < $TLE289);
{
    if (local_TLE290 == NULL)
    {
      local_TLE290 = EG (uninitialized_zval_ptr);
      local_TLE290->refcount++;
    }
  zval** p_lhs = &local_TLE290;

    zval* left;
  if (local_TSt288 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt288;

    zval* right;
  if (local_TLE289 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE289;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  is_smaller_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// if (TLE290) goto L389 else goto L390;
{
     zval* p_cond;
  if (local_TLE290 == NULL)
    p_cond = EG (uninitialized_zval_ptr);
  else
    p_cond = local_TLE290;

   zend_bool bcond = zend_is_true (p_cond);
   if (bcond)
      goto L389;
   else
      goto L390;
phc_check_invariants (TSRMLS_C);
}
// L389:
L389:;
// $TSt291 =& $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	zval** field = Z_OBJ_HT_PP(p_obj)->get_property_ptr_ptr(*p_obj, &field_name TSRMLS_CC);
	sep_copy_on_write (field);
	  if (local_TSt291 == NULL)
    {
      local_TSt291 = EG (uninitialized_zval_ptr);
      local_TSt291->refcount++;
    }
  zval** p_lhs = &local_TSt291;

	copy_into_ref (p_lhs, field);
phc_check_invariants (TSRMLS_C);
}
// $TLE292 = 65535;
{
        if (local_TLE292 == NULL)
    {
      local_TLE292 = EG (uninitialized_zval_ptr);
      local_TLE292->refcount++;
    }
  zval** p_lhs = &local_TLE292;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65535);

phc_check_invariants (TSRMLS_C);
}
// $TSt291 = ($TSt291 & $TLE292);
{
    if (local_TSt291 == NULL)
    {
      local_TSt291 = EG (uninitialized_zval_ptr);
      local_TSt291->refcount++;
    }
  zval** p_lhs = &local_TSt291;

    zval* left;
  if (local_TSt291 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt291;

    zval* right;
  if (local_TLE292 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE292;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// goto L391;
{
goto L391;
phc_check_invariants (TSRMLS_C);
}
// L390:
L390:;
// goto L391;
{
goto L391;
phc_check_invariants (TSRMLS_C);
}
// L391:
L391:;
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE286 != NULL)
{
zval_ptr_dtor (&local_TLE286);
}
if (local_TLE287 != NULL)
{
zval_ptr_dtor (&local_TLE287);
}
if (local_TLE289 != NULL)
{
zval_ptr_dtor (&local_TLE289);
}
if (local_TLE290 != NULL)
{
zval_ptr_dtor (&local_TLE290);
}
if (local_TLE292 != NULL)
{
zval_ptr_dtor (&local_TLE292);
}
if (local_TSt285 != NULL)
{
zval_ptr_dtor (&local_TSt285);
}
if (local_TSt288 != NULL)
{
zval_ptr_dtor (&local_TSt288);
}
if (local_TSt291 != NULL)
{
zval_ptr_dtor (&local_TSt291);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function setValue($value)
// {
// 	$TLE293 = 65535;
// 	$TLE294 = ($value % $TLE293);
// 	$this->_value = $TLE294;
// }
PHP_METHOD(UShort, setValue)
{
zval* local_TLE293 = NULL;
zval* local_TLE294 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TLE293 = 65535;
{
        if (local_TLE293 == NULL)
    {
      local_TLE293 = EG (uninitialized_zval_ptr);
      local_TLE293->refcount++;
    }
  zval** p_lhs = &local_TLE293;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65535);

phc_check_invariants (TSRMLS_C);
}
// $TLE294 = ($value % $TLE293);
{
    if (local_TLE294 == NULL)
    {
      local_TLE294 = EG (uninitialized_zval_ptr);
      local_TLE294->refcount++;
    }
  zval** p_lhs = &local_TLE294;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE293 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE293;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  mod_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->_value = $TLE294;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE294 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE294;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE293 != NULL)
{
zval_ptr_dtor (&local_TLE293);
}
if (local_TLE294 != NULL)
{
zval_ptr_dtor (&local_TLE294);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function bitAnd($value)
// {
// 	$TSt295 = $this->_value;
// 	$TLE296 = 65535;
// 	$TLE297 = ($value & $TLE296);
// 	$TLE298 = ($TSt295 & $TLE297);
// 	$this->_value = $TLE298;
// }
PHP_METHOD(UShort, bitAnd)
{
zval* local_TLE296 = NULL;
zval* local_TLE297 = NULL;
zval* local_TLE298 = NULL;
zval* local_TSt295 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TSt295 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt295 == NULL)
    {
      local_TSt295 = EG (uninitialized_zval_ptr);
      local_TSt295->refcount++;
    }
  zval** p_lhs = &local_TSt295;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE296 = 65535;
{
        if (local_TLE296 == NULL)
    {
      local_TLE296 = EG (uninitialized_zval_ptr);
      local_TLE296->refcount++;
    }
  zval** p_lhs = &local_TLE296;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65535);

phc_check_invariants (TSRMLS_C);
}
// $TLE297 = ($value & $TLE296);
{
    if (local_TLE297 == NULL)
    {
      local_TLE297 = EG (uninitialized_zval_ptr);
      local_TLE297->refcount++;
    }
  zval** p_lhs = &local_TLE297;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE296 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE296;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE298 = ($TSt295 & $TLE297);
{
    if (local_TLE298 == NULL)
    {
      local_TLE298 = EG (uninitialized_zval_ptr);
      local_TLE298->refcount++;
    }
  zval** p_lhs = &local_TLE298;

    zval* left;
  if (local_TSt295 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt295;

    zval* right;
  if (local_TLE297 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE297;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->_value = $TLE298;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE298 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE298;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE296 != NULL)
{
zval_ptr_dtor (&local_TLE296);
}
if (local_TLE297 != NULL)
{
zval_ptr_dtor (&local_TLE297);
}
if (local_TLE298 != NULL)
{
zval_ptr_dtor (&local_TLE298);
}
if (local_TSt295 != NULL)
{
zval_ptr_dtor (&local_TSt295);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function bitOr($value)
// {
// 	$TSt299 = $this->_value;
// 	$TLE300 = 65535;
// 	$TLE301 = ($value & $TLE300);
// 	$TLE302 = ($TSt299 | $TLE301);
// 	$this->_value = $TLE302;
// }
PHP_METHOD(UShort, bitOr)
{
zval* local_TLE300 = NULL;
zval* local_TLE301 = NULL;
zval* local_TLE302 = NULL;
zval* local_TSt299 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TSt299 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt299 == NULL)
    {
      local_TSt299 = EG (uninitialized_zval_ptr);
      local_TSt299->refcount++;
    }
  zval** p_lhs = &local_TSt299;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE300 = 65535;
{
        if (local_TLE300 == NULL)
    {
      local_TLE300 = EG (uninitialized_zval_ptr);
      local_TLE300->refcount++;
    }
  zval** p_lhs = &local_TLE300;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65535);

phc_check_invariants (TSRMLS_C);
}
// $TLE301 = ($value & $TLE300);
{
    if (local_TLE301 == NULL)
    {
      local_TLE301 = EG (uninitialized_zval_ptr);
      local_TLE301->refcount++;
    }
  zval** p_lhs = &local_TLE301;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE300 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE300;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE302 = ($TSt299 | $TLE301);
{
    if (local_TLE302 == NULL)
    {
      local_TLE302 = EG (uninitialized_zval_ptr);
      local_TLE302->refcount++;
    }
  zval** p_lhs = &local_TLE302;

    zval* left;
  if (local_TSt299 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt299;

    zval* right;
  if (local_TLE301 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE301;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_or_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->_value = $TLE302;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE302 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE302;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE300 != NULL)
{
zval_ptr_dtor (&local_TLE300);
}
if (local_TLE301 != NULL)
{
zval_ptr_dtor (&local_TLE301);
}
if (local_TLE302 != NULL)
{
zval_ptr_dtor (&local_TLE302);
}
if (local_TSt299 != NULL)
{
zval_ptr_dtor (&local_TSt299);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function bitXOr($value)
// {
// 	$TSt303 = $this->_value;
// 	$TLE304 = 65535;
// 	$TLE305 = ($value & $TLE304);
// 	$TLE306 = ($TSt303 ^ $TLE305);
// 	$this->_value = $TLE306;
// }
PHP_METHOD(UShort, bitXOr)
{
zval* local_TLE304 = NULL;
zval* local_TLE305 = NULL;
zval* local_TLE306 = NULL;
zval* local_TSt303 = NULL;
zval* local_this = getThis();
zval* local_value = NULL;
// Add all parameters as local variables
{
int num_args = ZEND_NUM_ARGS ();
zval* params[1];
zend_get_parameters_array(0, num_args, params);
// param 0
params[0]->refcount++;
if (local_value != NULL)
{
	zval_ptr_dtor (&local_value);
}
local_value = params[0];
}
// Function body
// $TSt303 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt303 == NULL)
    {
      local_TSt303 = EG (uninitialized_zval_ptr);
      local_TSt303->refcount++;
    }
  zval** p_lhs = &local_TSt303;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE304 = 65535;
{
        if (local_TLE304 == NULL)
    {
      local_TLE304 = EG (uninitialized_zval_ptr);
      local_TLE304->refcount++;
    }
  zval** p_lhs = &local_TLE304;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65535);

phc_check_invariants (TSRMLS_C);
}
// $TLE305 = ($value & $TLE304);
{
    if (local_TLE305 == NULL)
    {
      local_TLE305 = EG (uninitialized_zval_ptr);
      local_TLE305->refcount++;
    }
  zval** p_lhs = &local_TLE305;

    zval* left;
  if (local_value == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_value;

    zval* right;
  if (local_TLE304 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE304;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE306 = ($TSt303 ^ $TLE305);
{
    if (local_TLE306 == NULL)
    {
      local_TLE306 = EG (uninitialized_zval_ptr);
      local_TLE306->refcount++;
    }
  zval** p_lhs = &local_TLE306;

    zval* left;
  if (local_TSt303 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TSt303;

    zval* right;
  if (local_TLE305 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE305;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_xor_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->_value = $TLE306;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE306 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE306;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE304 != NULL)
{
zval_ptr_dtor (&local_TLE304);
}
if (local_TLE305 != NULL)
{
zval_ptr_dtor (&local_TLE305);
}
if (local_TLE306 != NULL)
{
zval_ptr_dtor (&local_TLE306);
}
if (local_TSt303 != NULL)
{
zval_ptr_dtor (&local_TSt303);
}
if (local_value != NULL)
{
zval_ptr_dtor (&local_value);
}
}
// public function bitNot()
// {
// 	$TSt307 = $this->_value;
// 	$TLE308 = ~$TSt307;
// 	$TLE309 = 65535;
// 	$TLE310 = ($TLE308 & $TLE309);
// 	$this->_value = $TLE310;
// }
PHP_METHOD(UShort, bitNot)
{
zval* local_TLE308 = NULL;
zval* local_TLE309 = NULL;
zval* local_TLE310 = NULL;
zval* local_TSt307 = NULL;
zval* local_this = getThis();
// Function body
// $TSt307 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt307 == NULL)
    {
      local_TSt307 = EG (uninitialized_zval_ptr);
      local_TSt307->refcount++;
    }
  zval** p_lhs = &local_TSt307;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// $TLE308 = ~$TSt307;
{
     if (local_TLE308 == NULL)
    {
      local_TLE308 = EG (uninitialized_zval_ptr);
      local_TLE308->refcount++;
    }
  zval** p_lhs = &local_TLE308;

     zval* rhs;
  if (local_TSt307 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TSt307;

   if (in_copy_on_write (*p_lhs))
   {
     zval_ptr_dtor (p_lhs);
     ALLOC_INIT_ZVAL (*p_lhs);
   }

   zval old = **p_lhs;
   int result_is_operand = (*p_lhs == rhs);
   bitwise_not_function (*p_lhs, rhs TSRMLS_CC);
   if (!result_is_operand)
	zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $TLE309 = 65535;
{
        if (local_TLE309 == NULL)
    {
      local_TLE309 = EG (uninitialized_zval_ptr);
      local_TLE309->refcount++;
    }
  zval** p_lhs = &local_TLE309;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_LONG (value, 65535);

phc_check_invariants (TSRMLS_C);
}
// $TLE310 = ($TLE308 & $TLE309);
{
    if (local_TLE310 == NULL)
    {
      local_TLE310 = EG (uninitialized_zval_ptr);
      local_TLE310->refcount++;
    }
  zval** p_lhs = &local_TLE310;

    zval* left;
  if (local_TLE308 == NULL)
    left = EG (uninitialized_zval_ptr);
  else
    left = local_TLE308;

    zval* right;
  if (local_TLE309 == NULL)
    right = EG (uninitialized_zval_ptr);
  else
    right = local_TLE309;

  if (in_copy_on_write (*p_lhs))
    {
      zval_ptr_dtor (p_lhs);
      ALLOC_INIT_ZVAL (*p_lhs);
    }

  zval old = **p_lhs;
  int result_is_operand = (*p_lhs == left || *p_lhs == right);
  bitwise_and_function (*p_lhs, left, right TSRMLS_CC);

  // If the result is one of the operands, the operator function
  // will already have cleaned up the result
  if (!result_is_operand)
    zval_dtor (&old);
phc_check_invariants (TSRMLS_C);
}
// $this->_value = $TLE310;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

	  zval* rhs;
  if (local_TLE310 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE310;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	Z_OBJ_HT_PP(p_obj)->write_property(*p_obj, &field_name, rhs TSRMLS_CC);
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE308 != NULL)
{
zval_ptr_dtor (&local_TLE308);
}
if (local_TLE309 != NULL)
{
zval_ptr_dtor (&local_TLE309);
}
if (local_TLE310 != NULL)
{
zval_ptr_dtor (&local_TLE310);
}
if (local_TSt307 != NULL)
{
zval_ptr_dtor (&local_TSt307);
}
}
// public function getValue()
// {
// 	$TSt311 = $this->_value;
// 	return $TSt311;
// }
PHP_METHOD(UShort, getValue)
{
zval* local_TSt311 = NULL;
zval* local_this = getThis();
// Function body
// $TSt311 = $this->_value;
{
	  if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

		zval field_name;
	INIT_ZVAL (field_name);
	ZVAL_STRING (&field_name, "_value", 0);

	// I *think* this is correct, but documentation of the Zend API is scarce :)
	zval* field = Z_OBJ_HT_PP(p_obj)->read_property(*p_obj, &field_name, BP_VAR_R TSRMLS_CC);
	  if (local_TSt311 == NULL)
    {
      local_TSt311 = EG (uninitialized_zval_ptr);
      local_TSt311->refcount++;
    }
  zval** p_lhs = &local_TSt311;

	write_var (p_lhs, field); 
phc_check_invariants (TSRMLS_C);
}
// return $TSt311;
{
     zval* rhs;
  if (local_TSt311 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TSt311;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TSt311 != NULL)
{
zval_ptr_dtor (&local_TSt311);
}
}
// public function __toString()
// {
// 	$TLE312 = $this->getValue();
// 	$TLE313 = (string) $TLE312;
// 	return $TLE313;
// }
PHP_METHOD(UShort, __toString)
{
zval* local_TLE312 = NULL;
zval* local_TLE313 = NULL;
zval* local_this = getThis();
// Function body
// $TLE312 = $this->getValue();
{
     if (local_this == NULL)
    {
      local_this = EG (uninitialized_zval_ptr);
      local_this->refcount++;
    }
  zval** p_obj = &local_this;

   zend_fcall_info fci_object;
   zend_fcall_info_cache fcic_object = {0, NULL, NULL, NULL};
   initialize_method_call (&fci_object, &fcic_object, p_obj, "getValue", "tools.source.php", 313 TSRMLS_CC);
      zend_function* signature = fcic_object.function_handler;
   zend_arg_info* arg_info = signature->common.arg_info; // optional

   int by_ref[0];
   int abr_index = 0;
   

   // Setup array of arguments
   // TODO: i think arrays of size 0 is an error
   int destruct [0];
   zval* args [0];
   zval** args_ind [0];

   int af_index = 0;
   

   phc_setup_error (1, "tools.source.php", 313, NULL TSRMLS_CC);

   // save existing parameters, in case of recursion
   int param_count_save = fci_object.param_count;
   zval*** params_save = fci_object.params;
   zval** retval_save = fci_object.retval_ptr_ptr;

   zval* rhs = NULL;

   // set up params
   fci_object.params = args_ind;
   fci_object.param_count = 0;
   fci_object.retval_ptr_ptr = &rhs;

   // call the function
   int success = zend_call_function (&fci_object, &fcic_object TSRMLS_CC);
   assert(success == SUCCESS);

   // restore params
   fci_object.params = params_save;
   fci_object.param_count = param_count_save;
   fci_object.retval_ptr_ptr = retval_save;

   // unset the errors
   phc_setup_error (0, NULL, 0, NULL TSRMLS_CC);

   int i;
   for (i = 0; i < 0; i++)
   {
      if (destruct[i])
      {
	 assert (destruct[i]);
	 zval_ptr_dtor (args_ind[i]);
      }
   }


   // When the Zend engine returns by reference, it allocates a zval into
   // retval_ptr_ptr. To return by reference, the callee writes into the
   // retval_ptr_ptr, freeing the allocated value as it does.  (Note, it may
   // not actually return anything). So the zval returned - whether we return
   // it, or it is the allocated zval - has a refcount of 1.
 
   // The caller is responsible for cleaning that up (note, this is unaffected
   // by whether it is added to some COW set).

   // For reasons unknown, the Zend API resets the refcount and is_ref fields
   // of the return value after the function returns (unless the callee is
   // interpreted). If the function is supposed to return by reference, this
   // loses the refcount. This only happens when non-interpreted code is
   // called. We work around it, when compiled code is called, by saving the
   // refcount into SAVED_REFCOUNT, in the return statement. The downside is
   // that we may create an error if our code is called by a callback, and
   // returns by reference, and the callback returns by reference. At least
   // this is an obscure case.
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
   {
      assert (rhs != EG(uninitialized_zval_ptr));
      rhs->is_ref = 1;
      if (saved_refcount != 0)
      {
	 rhs->refcount = saved_refcount;
      }
      rhs->refcount++;
   }
   saved_refcount = 0; // for 'obscure cases'

        if (local_TLE312 == NULL)
    {
      local_TLE312 = EG (uninitialized_zval_ptr);
      local_TLE312->refcount++;
    }
  zval** p_lhs = &local_TLE312;

   write_var (p_lhs, rhs);


   zval_ptr_dtor (&rhs);
   if(signature->common.return_reference && signature->type != ZEND_USER_FUNCTION)
      zval_ptr_dtor (&rhs);

phc_check_invariants (TSRMLS_C);
}
// $TLE313 = (string) $TLE312;
{
      if (local_TLE313 == NULL)
    {
      local_TLE313 = EG (uninitialized_zval_ptr);
      local_TLE313->refcount++;
    }
  zval** p_lhs = &local_TLE313;

    zval* rhs;
  if (local_TLE312 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE312;

  if (*p_lhs != rhs)
    {
        if ((*p_lhs)->is_ref)
      overwrite_lhs (*p_lhs, rhs);
  else
    {
      zval_ptr_dtor (p_lhs);
        if (rhs->is_ref)
    {
      // Take a copy of RHS for LHS
      *p_lhs = zvp_clone_ex (rhs);
    }
  else
    {
      // Share a copy
      rhs->refcount++;
      *p_lhs = rhs;
    }

    }

    }

    assert (IS_STRING >= 0 && IS_STRING <= 6);
  if ((*p_lhs)->type != IS_STRING)
  {
    sep_copy_on_write (p_lhs);
    convert_to_string (*p_lhs);
  }

phc_check_invariants (TSRMLS_C);
}
// return $TLE313;
{
     zval* rhs;
  if (local_TLE313 == NULL)
    rhs = EG (uninitialized_zval_ptr);
  else
    rhs = local_TLE313;

   // Run-time return by reference has different semantics to compile-time.
   // If the function has CTRBR and RTRBR, the the assignment will be
   // reference. If one or the other is return-by-copy, the result will be
   // by copy. Its a question of whether its separated at return-time (which
   // we do here) or at the call-site.
   return_value->value = rhs->value;
   return_value->type = rhs->type;
   zval_copy_ctor (return_value);
   goto end_of_function;
phc_check_invariants (TSRMLS_C);
}
// Method exit
end_of_function:__attribute__((unused));
if (local_TLE312 != NULL)
{
zval_ptr_dtor (&local_TLE312);
}
if (local_TLE313 != NULL)
{
zval_ptr_dtor (&local_TLE313);
}
}
// ArgInfo structures (necessary to support compile time pass-by-reference)
ZEND_BEGIN_ARG_INFO_EX(UShort___construct_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UShort_add_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UShort_subtract_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UShort_setValue_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UShort_bitAnd_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UShort_bitOr_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UShort_bitXOr_arg_info, 0, 0, 0)
ZEND_ARG_INFO(0, "value")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UShort_bitNot_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UShort_getValue_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UShort___toString_arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

static function_entry UShort_functions[] = {
PHP_ME(UShort, __construct, UShort___construct_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UShort, add, UShort_add_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UShort, subtract, UShort_subtract_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UShort, setValue, UShort_setValue_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UShort, bitAnd, UShort_bitAnd_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UShort, bitOr, UShort_bitOr_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UShort, bitXOr, UShort_bitXOr_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UShort, bitNot, UShort_bitNot_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UShort, getValue, UShort_getValue_arg_info, ZEND_ACC_PUBLIC)
PHP_ME(UShort, __toString, UShort___toString_arg_info, ZEND_ACC_PUBLIC)
{ NULL, NULL, NULL }
};
// function __MAIN__()
// {
// }
PHP_FUNCTION(__MAIN__)
{
// Function body
// Method exit
end_of_function:__attribute__((unused));
}
// Module initialization
PHP_MINIT_FUNCTION(prnltools)
{
init_runtime();
saved_refcount = 0;

{
zend_class_entry ce; // temp
zend_class_entry* ce_reg; // once registered, ce_ptr should be used
INIT_CLASS_ENTRY(ce, "Endian", Endian_functions);
ce_reg = zend_register_internal_class(&ce TSRMLS_CC);
ce_reg->type &= ~ZEND_INTERNAL_CLASS;
}{
zend_class_entry ce; // temp
zend_class_entry* ce_reg; // once registered, ce_ptr should be used
INIT_CLASS_ENTRY(ce, "Memory", Memory_functions);
ce_reg = zend_register_internal_class(&ce TSRMLS_CC);
ce_reg->type &= ~ZEND_INTERNAL_CLASS;
{
zval* default_value;
{
zval* local___static_value__ = NULL;
// $__static_value__ = NULL;
{
        if (local___static_value__ == NULL)
    {
      local___static_value__ = EG (uninitialized_zval_ptr);
      local___static_value__->refcount++;
    }
  zval** p_lhs = &local___static_value__;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_NULL (value);

phc_check_invariants (TSRMLS_C);
}
default_value = local___static_value__;
assert(!default_value->is_ref);
default_value->refcount++;
if (local___static_value__ != NULL)
{
zval_ptr_dtor (&local___static_value__);
}
}
phc_declare_property(ce_reg, "_mem", 4, default_value, ZEND_ACC_PUBLIC TSRMLS_CC);}{
zval* default_value;
{
zval* local___static_value__ = NULL;
// $__static_value__ = NULL;
{
        if (local___static_value__ == NULL)
    {
      local___static_value__ = EG (uninitialized_zval_ptr);
      local___static_value__->refcount++;
    }
  zval** p_lhs = &local___static_value__;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_NULL (value);

phc_check_invariants (TSRMLS_C);
}
default_value = local___static_value__;
assert(!default_value->is_ref);
default_value->refcount++;
if (local___static_value__ != NULL)
{
zval_ptr_dtor (&local___static_value__);
}
}
phc_declare_property(ce_reg, "_buffer", 7, default_value, ZEND_ACC_PUBLIC TSRMLS_CC);}{
zval* default_value;
{
zval* local___static_value__ = NULL;
// $__static_value__ = NULL;
{
        if (local___static_value__ == NULL)
    {
      local___static_value__ = EG (uninitialized_zval_ptr);
      local___static_value__->refcount++;
    }
  zval** p_lhs = &local___static_value__;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_NULL (value);

phc_check_invariants (TSRMLS_C);
}
default_value = local___static_value__;
assert(!default_value->is_ref);
default_value->refcount++;
if (local___static_value__ != NULL)
{
zval_ptr_dtor (&local___static_value__);
}
}
phc_declare_property(ce_reg, "_pos", 4, default_value, ZEND_ACC_PUBLIC TSRMLS_CC);}{
zval* default_value;
{
zval* local___static_value__ = NULL;
// $__static_value__ = NULL;
{
        if (local___static_value__ == NULL)
    {
      local___static_value__ = EG (uninitialized_zval_ptr);
      local___static_value__->refcount++;
    }
  zval** p_lhs = &local___static_value__;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_NULL (value);

phc_check_invariants (TSRMLS_C);
}
default_value = local___static_value__;
assert(!default_value->is_ref);
default_value->refcount++;
if (local___static_value__ != NULL)
{
zval_ptr_dtor (&local___static_value__);
}
}
phc_declare_property(ce_reg, "_readPos", 8, default_value, ZEND_ACC_PUBLIC TSRMLS_CC);}}{
zend_class_entry ce; // temp
zend_class_entry* ce_reg; // once registered, ce_ptr should be used
INIT_CLASS_ENTRY(ce, "UByte", UByte_functions);
ce_reg = zend_register_internal_class(&ce TSRMLS_CC);
ce_reg->type &= ~ZEND_INTERNAL_CLASS;
{
zval* default_value;
{
zval* local___static_value__ = NULL;
// $__static_value__ = NULL;
{
        if (local___static_value__ == NULL)
    {
      local___static_value__ = EG (uninitialized_zval_ptr);
      local___static_value__->refcount++;
    }
  zval** p_lhs = &local___static_value__;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_NULL (value);

phc_check_invariants (TSRMLS_C);
}
default_value = local___static_value__;
assert(!default_value->is_ref);
default_value->refcount++;
if (local___static_value__ != NULL)
{
zval_ptr_dtor (&local___static_value__);
}
}
phc_declare_property(ce_reg, "_value", 6, default_value, ZEND_ACC_PUBLIC TSRMLS_CC);}}{
zend_class_entry ce; // temp
zend_class_entry* ce_reg; // once registered, ce_ptr should be used
INIT_CLASS_ENTRY(ce, "UShort", UShort_functions);
ce_reg = zend_register_internal_class(&ce TSRMLS_CC);
ce_reg->type &= ~ZEND_INTERNAL_CLASS;
{
zval* default_value;
{
zval* local___static_value__ = NULL;
// $__static_value__ = NULL;
{
        if (local___static_value__ == NULL)
    {
      local___static_value__ = EG (uninitialized_zval_ptr);
      local___static_value__->refcount++;
    }
  zval** p_lhs = &local___static_value__;

   zval* value;
   if ((*p_lhs)->is_ref)
   {
     // Always overwrite the current value
     value = *p_lhs;
     zval_dtor (value);
   }
   else
   {
     ALLOC_INIT_ZVAL (value);
     zval_ptr_dtor (p_lhs);
     *p_lhs = value;
   }

   ZVAL_NULL (value);

phc_check_invariants (TSRMLS_C);
}
default_value = local___static_value__;
assert(!default_value->is_ref);
default_value->refcount++;
if (local___static_value__ != NULL)
{
zval_ptr_dtor (&local___static_value__);
}
}
phc_declare_property(ce_reg, "_value", 6, default_value, ZEND_ACC_PUBLIC TSRMLS_CC);}}return SUCCESS;}// ArgInfo structures (necessary to support compile time pass-by-reference)
ZEND_BEGIN_ARG_INFO_EX(prnltools___MAIN___arg_info, 0, 0, 0)
ZEND_END_ARG_INFO()

static function_entry prnltools_functions[] = {
PHP_FE(__MAIN__, prnltools___MAIN___arg_info)
{ NULL, NULL, NULL }
};
// Register the module itself with PHP
zend_module_entry prnltools_module_entry = {
STANDARD_MODULE_HEADER, 
"prnltools",
prnltools_functions,
PHP_MINIT(prnltools), /* MINIT */
NULL, /* MSHUTDOWN */
NULL, /* RINIT */
NULL, /* RSHUTDOWN */
NULL, /* MINFO */
"1.0",
STANDARD_MODULE_PROPERTIES
};
ZEND_GET_MODULE(prnltools)
