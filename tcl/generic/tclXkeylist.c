/* 
 * tclXkeylist.c --
 *
 *  Extended Tcl keyed list commands and interfaces.
 *-----------------------------------------------------------------------------
 * Copyright 1991-1997 Karl Lehenbauer and Mark Diekhans.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies.  Karl Lehenbauer and
 * Mark Diekhans make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *-----------------------------------------------------------------------------
 * $Id: tclXkeylist.c,v 8.3 1997/06/25 08:23:38 markd Exp $
 *-----------------------------------------------------------------------------
 */

#include "tclExtdInt.h"

/*
 * Keyed lists are stored as arrays recursively defined objects.  The data
 * portion of a keyed list entry is a Tcl_Obj which may be a keyed list object
 * or any other Tcl object.  Since determine the structure of a keyed list is
 * lazy (you don't know if an element is data or another keyed list) until it
 * is accessed, the object can be transformed into a keyed list from a Tcl
 * string or list.
 */

/*
 * An entry in a keyed list array.   (FIX: Should key be object?)
 */
typedef struct {
    char    *key;
    Tcl_Obj *valuePtr;
} keylEntry_t;

/*
 * Internal representation of a keyed list object.
 */
typedef struct {
    int          arraySize;   /* Current slots available in the array.  */
    int          numEntries;  /* Number of actual entries in the array. */
    keylEntry_t *entries;     /* Array of keyed list entries.           */
} keylIntObj_t;

/*
 * Amount to increment array size by when it needs to grow.
 */
#define KEYEDLIST_ARRAY_INCR_SIZE 16

/*
 * Invalidate the string representation (FIX: should be in Tcl).
 */
#define TclX_InvalidateStringRep(objPtr) \
    if (objPtr->bytes != NULL) { \
        ckfree ((char *) objPtr->bytes); \
        keylPtr->bytes = NULL; \
    }

/*
 * Macro to duplicate a child entry of a keyed list if it is share by more
 * than the parent.
 */
#define DupSharedKeyListChild(keylIntPtr, idx) \
    if (Tcl_IsShared (keylIntPtr->entries [idx].valuePtr)) { \
        keylIntPtr->entries [idx].valuePtr = \
            Tcl_DuplicateObj (keylIntPtr->entries [idx].valuePtr); \
        Tcl_IncrRefCount (keylIntPtr->entries [idx].valuePtr); \
    }

/*
 * Macros to validate an keyed list object or internal representation
 */
#ifdef TCLX_DEBUG
#   define KEYL_OBJ_ASSERT(keylAPtr) {\
        assert (keylAPtr->typePtr == &keyedListType); \
        ValidateKeyedList (keylAIntPtr); \
    }
#   define KEYL_REP_ASSERT(keylAIntPtr) \
        ValidateKeyedList (keylAIntPtr)
#else
#  define KEYL_REP_ASSERT(keylAIntPtr)
#endif


/*
 * Prototypes of internal functions.
 */
static void
ValidateKeyedList _ANSI_ARGS_((keylIntObj_t *keylIntPtr));

static int
ValidateKey _ANSI_ARGS_((Tcl_Interp *interp,
                         char *key,
                         int keyLen,
                         int isPath));

static keylIntObj_t *
AllocKeyedListIntRep _ANSI_ARGS_((void));

static void
FreeKeyedListData _ANSI_ARGS_((keylIntObj_t *keylIntPtr));

static void
EnsureKeyedListSpace _ANSI_ARGS_((keylIntObj_t *keylIntPtr,
                                  int           newNumEntries));

static void
DeleteKeyedListEntry _ANSI_ARGS_((keylIntObj_t *keylIntPtr,
                                  int           entryIdx));

static int
FindKeyedListEntry _ANSI_ARGS_((keylIntObj_t *keylIntPtr,
                                char         *key,
                                int          *keyLenPtr,
                                char        **nextSubKeyPtr));

static int
ObjToKeyedListEntry _ANSI_ARGS_((Tcl_Interp  *interp,
                                 Tcl_Obj     *objPtr,
                                 keylEntry_t *entryPtr));

static void
DupKeyedListInternalRep _ANSI_ARGS_((Tcl_Obj *srcPtr,
                                     Tcl_Obj *copyPtr));

static void
FreeKeyedListInternalRep _ANSI_ARGS_((Tcl_Obj *keylPtr));

static int
SetKeyedListFromAny _ANSI_ARGS_((Tcl_Interp *interp,
                                 Tcl_Obj    *objPtr));

static void
UpdateStringOfKeyedList _ANSI_ARGS_((Tcl_Obj *keylPtr));

static int 
TclX_KeylgetObjCmd _ANSI_ARGS_((ClientData   clientData,
                                Tcl_Interp  *interp,
                                int          objc,
                                Tcl_Obj     *CONST objv[]));

static int
TclX_KeylsetObjCmd _ANSI_ARGS_((ClientData   clientData,
                                Tcl_Interp  *interp,
                                int          objc,
                                Tcl_Obj     *CONST objv[]));

static int 
TclX_KeyldelObjCmd _ANSI_ARGS_((ClientData   clientData,
                                Tcl_Interp  *interp,
                                int          objc,
                                Tcl_Obj     *CONST objv[]));

static int 
TclX_KeylkeysObjCmd _ANSI_ARGS_((ClientData   clientData,
                                 Tcl_Interp  *interp,
                                 int          objc,
                                 Tcl_Obj     *CONST objv[]));

/*
 * Type definition.
 */
static Tcl_ObjType keyedListType = {
    "keyedList",              /* name */
    FreeKeyedListInternalRep, /* freeIntRepProc */
    DupKeyedListInternalRep,  /* dupIntRepProc */
    UpdateStringOfKeyedList,  /* updateStringProc */
    SetKeyedListFromAny       /* setFromAnyProc */
};


/*-----------------------------------------------------------------------------
 * ValidateKeyedList --
 *   Validate a keyed list (only when TCLX_DEBUG is enabled).
 * Parameters:
 *   o keylIntPtr - Keyed list internal representation.
 *-----------------------------------------------------------------------------
 */
#ifdef TCLX_DEBUG
static void
ValidateKeyedList (keylIntPtr)
    keylIntObj_t *keylIntPtr;
{
    int idx;

    assert (keylIntPtr->arraySize >= keylIntPtr->numEntries);
    assert (keylIntPtr->arraySize >= 0);
    assert (keylIntPtr->numEntries >= 0);
    assert ((keylIntPtr->arraySize > 0) ?
            (keylIntPtr->entries != NULL) : TRUE);
    assert ((keylIntPtr->numEntries > 0) ?
            (keylIntPtr->entries != NULL) : TRUE);

    for (idx = 0; idx < keylIntPtr->numEntries; idx++) {
        keylEntry_t *entryPtr = &(keylIntPtr->entries [idx]);
        assert (entryPtr->key != NULL);
        assert (entryPtr->valuePtr->refCount >= 1);
        if (entryPtr->valuePtr->typePtr == &keyedListType) {
            ValidateKeyedList (entryPtr->valuePtr->internalRep.otherValuePtr);
        }
    }
}
#endif

/*-----------------------------------------------------------------------------
 * ValidateKey --
 *   Check that a key or keypath string is a valid value.
 *
 * Parameters:
 *   o interp - Used to return error messages.
 *   o key - Key string to check.
 *   o keyLen - Length of the string, used to check for binary data.
 *   o isPath - TRUE if this is a key path, FALSE if its a simple key and
 *     thus "." is illegal.
 * Returns:
 *    TCL_OK or TCL_ERROR.
 *-----------------------------------------------------------------------------
 */
static int
ValidateKey (interp, key, keyLen, isPath)
    Tcl_Interp *interp;
    char *key;
    int keyLen;
    int isPath;
{
    char *keyp;

    if (strlen (key) != keyLen) {
        Tcl_AppendStringsToObj (Tcl_GetObjResult (interp),
                                "keyed list key may not be a ",
                                "binary string", (char *) NULL);
        return TCL_ERROR;
    }
    if (key [0] == '\0') {
        Tcl_AppendStringsToObj (Tcl_GetObjResult (interp),
                                "keyed list key may not be an ",
                                "empty string", (char *) NULL);
        return TCL_ERROR;
    }
    for (keyp = key; *keyp != '\0'; keyp++) {
        if ((!isPath) && (*keyp == '.')) {
            Tcl_AppendStringsToObj (Tcl_GetObjResult (interp),
                                    "keyed list key may not contain a \".\"; ",
                                    "it is used as a separator in key paths",
                                    (char *) NULL);
            return TCL_ERROR;
        }
        if (isspace (*keyp)) {
            Tcl_AppendStringsToObj (Tcl_GetObjResult (interp),
                                    "keyed list key may not contain ",
                                    "white-space characters", (char *) NULL);
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}


/*-----------------------------------------------------------------------------
 * AllocKeyedListIntRep --
 *   Allocate an and initialize the keyed list internal representation.
 *
 * Returns:
 *    A pointer to the keyed list internal structure.
 *-----------------------------------------------------------------------------
 */
static keylIntObj_t *
AllocKeyedListIntRep ()
{
    keylIntObj_t *keylIntPtr;

    keylIntPtr = (keylIntObj_t *) ckalloc (sizeof (keylIntObj_t));

    keylIntPtr->arraySize = 0;
    keylIntPtr->numEntries = 0;
    keylIntPtr->entries = NULL;

    return keylIntPtr;
}

/*-----------------------------------------------------------------------------
 * FreeKeyedListData --
 *   Free the internal representation of a keyed list.
 *
 * Parameters:
 *   o keylIntPtr - Keyed list internal structure to free.
 *-----------------------------------------------------------------------------
 */
static void
FreeKeyedListData (keylIntPtr)
    keylIntObj_t *keylIntPtr;
{
    int idx;

    for (idx = 0; idx < keylIntPtr->numEntries ; idx++) {
        ckfree (keylIntPtr->entries [idx].key);
        Tcl_DecrRefCount (keylIntPtr->entries [idx].valuePtr);
    }
    if (keylIntPtr->entries != NULL)
        ckfree ((VOID*) keylIntPtr->entries);
    ckfree ((VOID*) keylIntPtr);
}

/*-----------------------------------------------------------------------------
 * EnsureKeyedListSpace --
 *   Ensure there is enough room in a keyed list array for a certain number
 * of entries, expanding if necessary.
 *
 * Parameters:
 *   o keylIntPtr - Keyed list internal representation.
 *   o newNumEntries - The number of entries that are going to be added to
 *     the keyed list.
 *-----------------------------------------------------------------------------
 */
static void
EnsureKeyedListSpace (keylIntPtr, newNumEntries)
    keylIntObj_t *keylIntPtr;
    int           newNumEntries;
{
    KEYL_REP_ASSERT (keylIntPtr);

    if ((keylIntPtr->arraySize - keylIntPtr->numEntries) < newNumEntries) {
        int newSize = keylIntPtr->arraySize + newNumEntries +
            KEYEDLIST_ARRAY_INCR_SIZE;
        if (keylIntPtr->entries == NULL) {
            keylIntPtr->entries = (keylEntry_t *)
                ckalloc (newSize * sizeof (keylEntry_t));
        } else {
            keylIntPtr->entries = (keylEntry_t *)
                ckrealloc ((VOID *) keylIntPtr->entries,
                           newSize * sizeof (keylEntry_t));
        }
        keylIntPtr->arraySize = newSize;
    }

    KEYL_REP_ASSERT (keylIntPtr);
}

/*-----------------------------------------------------------------------------
 * DeleteKeyedListEntry --
 *   Delete an entry from a keyed list.
 *
 * Parameters:
 *   o keylIntPtr - Keyed list internal representation.
 *   o entryIdx - Index of entry to delete.
 *-----------------------------------------------------------------------------
 */
static void
DeleteKeyedListEntry (keylIntPtr, entryIdx)
    keylIntObj_t *keylIntPtr;
    int           entryIdx;
{
    int idx;

    ckfree (keylIntPtr->entries [entryIdx].key);
    Tcl_DecrRefCount (keylIntPtr->entries [entryIdx].valuePtr);

    for (idx = entryIdx; idx < keylIntPtr->numEntries - 1; idx++)
        keylIntPtr->entries [idx] = keylIntPtr->entries [idx + 1];
    keylIntPtr->numEntries--;

    KEYL_REP_ASSERT (keylIntPtr);
}

/*-----------------------------------------------------------------------------
 * FindKeyedListEntry --
 *   Find an entry in keyed list.
 *
 * Parameters:
 *   o keylIntPtr - Keyed list internal representation.
 *   o key - Name of key to search for.
 *   o keyLenPtr - In not NULL, the length of the key for this
 *     level is returned here.  This excludes subkeys and the `.' delimiters.
 *   o nextSubKeyPtr - If not NULL, the start of the name of the next
 *     sub-key within key is returned.
 * Returns:
 *   Index of the entry or -1 if not found.
 *-----------------------------------------------------------------------------
 */
static int
FindKeyedListEntry (keylIntPtr, key, keyLenPtr, nextSubKeyPtr)
    keylIntObj_t *keylIntPtr;
    char         *key;
    int          *keyLenPtr;
    char        **nextSubKeyPtr;
{
    char *keySeparPtr;
    int keyLen, findIdx;

    keySeparPtr = strchr (key, '.');
    if (keySeparPtr != NULL) {
        keyLen = keySeparPtr - key;
    } else {
        keyLen = strlen (key);
    }

    for (findIdx = 0; findIdx < keylIntPtr->numEntries; findIdx++) {
        if (strncmp (keylIntPtr->entries [findIdx].key, key,
                     keyLen) == 0)
            break;
    }

    if (nextSubKeyPtr != NULL) {
        if (keySeparPtr == NULL) {
            *nextSubKeyPtr = NULL;
        } else {
            *nextSubKeyPtr = keySeparPtr + 1;
        }
    }
    if (keyLenPtr != NULL) {
        *keyLenPtr = keyLen;
    }
    
    if (findIdx >= keylIntPtr->numEntries) {
        return -1;
    }
    
    return findIdx;
}

/*-----------------------------------------------------------------------------
 * ObjToKeyedListEntry --
 *   Convert an object to a keyed list entry. (Keyword/value pair).
 *
 * Parameters:
 *   o interp - Used to return error messages, if not NULL.
 *   o objPtr - Object to convert.  Each entry must be a two element list,
 *     with the first element being the key and the second being the
 *     value.
 *   o entryPtr - The keyed list entry to initialize from the object.
 * Returns:
 *    TCL_OK or TCL_ERROR.
 *-----------------------------------------------------------------------------
 */
static int
ObjToKeyedListEntry (interp, objPtr, entryPtr)
    Tcl_Interp  *interp;
    Tcl_Obj     *objPtr;
    keylEntry_t *entryPtr;
{
    int objc;
    Tcl_Obj **objv;
    char *key;
    int keyLen;

    if (Tcl_ListObjGetElements (interp, objPtr, &objc, &objv) != TCL_OK) {
        Tcl_ResetResult (interp);
        Tcl_AppendStringsToObj (Tcl_GetObjResult (interp),
                                "keyed list entry not a valid list, ",
                                "found \"", 
                                Tcl_GetStringFromObj (objPtr, NULL),
                                "\"", (char *) NULL);
        return TCL_ERROR;
    }

    if (objc != 2) {
        Tcl_AppendStringsToObj (Tcl_GetObjResult (interp),
                                "keyed list entry must be a two ",
                                "element list, found \"",
                                Tcl_GetStringFromObj (objPtr, NULL),
                                "\"", (char *) NULL);
        return TCL_ERROR;
    }

    key = Tcl_GetStringFromObj (objv [0], &keyLen);
    if (ValidateKey (interp, key, keyLen, FALSE) == TCL_ERROR) {
        return TCL_ERROR;
    }

    entryPtr->key = ckstrdup (key);
    entryPtr->valuePtr = Tcl_DuplicateObj (objv [1]);
    Tcl_IncrRefCount (entryPtr->valuePtr);

    return TCL_OK;
}

/*-----------------------------------------------------------------------------
 * FreeKeyedListInternalRep --
 *   Free the internal representation of a keyed list.
 *
 * Parameters:
 *   o keylPtr - Keyed list object being deleted.
 *-----------------------------------------------------------------------------
 */
static void
FreeKeyedListInternalRep (keylPtr)
    Tcl_Obj *keylPtr;
{
    FreeKeyedListData ((keylIntObj_t *) keylPtr->internalRep.otherValuePtr);
}

/*-----------------------------------------------------------------------------
 * DupKeyedListInternalRep --
 *   Duplicate the internal representation of a keyed list.
 *
 * Parameters:
 *   o srcPtr - Keyed list object to copy.
 *   o copyPtr - Target object to copy internal representation to.
 *-----------------------------------------------------------------------------
 */
static void
DupKeyedListInternalRep (srcPtr, copyPtr)
    Tcl_Obj *srcPtr;
    Tcl_Obj *copyPtr;
{
    keylIntObj_t *srcIntPtr =
        (keylIntObj_t *) srcPtr->internalRep.otherValuePtr;
    keylIntObj_t *copyIntPtr;
    int idx;

    KEYL_REP_ASSERT (srcIntPtr);

    copyIntPtr = (keylIntObj_t *) ckalloc (sizeof (keylIntObj_t));
    copyIntPtr->arraySize = srcIntPtr->arraySize;
    copyIntPtr->numEntries = srcIntPtr->numEntries;
    copyIntPtr->entries = (keylEntry_t *)
        ckalloc (copyIntPtr->arraySize * sizeof (keylEntry_t));

    for (idx = 0; idx < srcIntPtr->numEntries ; idx++) {
        copyIntPtr->entries [idx].key = 
            ckstrdup (srcIntPtr->entries [idx].key);
        copyIntPtr->entries [idx].valuePtr = srcIntPtr->entries [idx].valuePtr;
        Tcl_IncrRefCount (copyIntPtr->entries [idx].valuePtr);
    }

    copyPtr->internalRep.otherValuePtr = (VOID *) copyIntPtr;
    copyPtr->typePtr = &keyedListType;

    KEYL_REP_ASSERT (copyIntPtr);
}

/*-----------------------------------------------------------------------------
 * SetKeyedListFromAny --
 *   Convert an object to a keyed list from its string representation.  Only
 * the first level is converted, as there is no way of knowing how far down
 * the keyed list recurses until lower levels are accessed.
 *
 * Parameters:
 *   o objPtr - Object to convert to a keyed list.
 *-----------------------------------------------------------------------------
 */
static int
SetKeyedListFromAny (interp, objPtr) 
    Tcl_Interp *interp;
    Tcl_Obj    *objPtr;
{
    keylIntObj_t *keylIntPtr;
    int idx, objc;
    Tcl_Obj **objv;

    if (Tcl_ListObjGetElements (interp, objPtr, &objc, &objv) != TCL_OK)
        return TCL_ERROR;
    
    keylIntPtr = AllocKeyedListIntRep ();

    EnsureKeyedListSpace (keylIntPtr, objc);

    for (idx = 0; idx < objc; idx++) {
        if (ObjToKeyedListEntry (interp, objv [idx], 
                &(keylIntPtr->entries [keylIntPtr->numEntries])) != TCL_OK)
            goto errorExit;
        keylIntPtr->numEntries++;
    }

    if ((objPtr->typePtr != NULL) &&
        (objPtr->typePtr->freeIntRepProc != NULL)) {
        (*objPtr->typePtr->freeIntRepProc) (objPtr);
    }
    objPtr->internalRep.otherValuePtr = (VOID *) keylIntPtr;
    objPtr->typePtr = &keyedListType;

    KEYL_REP_ASSERT (keylIntPtr);
    return TCL_OK;
    
  errorExit:
    FreeKeyedListData (keylIntPtr);
    return TCL_ERROR;
}

/*-----------------------------------------------------------------------------
 * UpdateStringOfKeyedList --
 *    Update the string representation of a keyed list.
 *
 * Parameters:
 *   o objPtr - Object to convert to a keyed list.
 *-----------------------------------------------------------------------------
 */
static void
UpdateStringOfKeyedList (keylPtr)
    Tcl_Obj  *keylPtr;
{
#define UPDATE_STATIC_SIZE 32
    int idx, strLen;
    Tcl_Obj **listObjv, *entryObjv [2], *tmpListObj;
    Tcl_Obj *staticListObjv [UPDATE_STATIC_SIZE];
    char *listStr;
    keylIntObj_t *keylIntPtr =
        (keylIntObj_t *) keylPtr->internalRep.otherValuePtr;

    /*
     * Conversion to strings is done via list objects to support binary data.
     */
    if (keylIntPtr->numEntries > UPDATE_STATIC_SIZE) {
        listObjv =
            (Tcl_Obj **) ckalloc (keylIntPtr->numEntries * sizeof (Tcl_Obj *));
    } else {
        listObjv = staticListObjv;
    }

    /*
     * Convert each keyed list entry to a two element list object.  No
     * need to incr/decr ref counts, the list objects will take care of that.
     * FIX: Keeping key as string object will speed this up.
     */
    for (idx = 0; idx < keylIntPtr->numEntries; idx++) {
        entryObjv [0] = 
            Tcl_NewStringObj (keylIntPtr->entries [idx].key,
                              strlen (keylIntPtr->entries [idx].key));
        entryObjv [1] = keylIntPtr->entries [idx].valuePtr;
        listObjv [idx] = Tcl_NewListObj (2, entryObjv);
    }

    tmpListObj = Tcl_NewListObj (keylIntPtr->numEntries, listObjv);
    listStr = Tcl_GetStringFromObj (tmpListObj, &strLen);
    keylPtr->bytes = ckbinstrdup (listStr, strLen);
    keylPtr->length = strLen;

    Tcl_DecrRefCount (tmpListObj);
    if (listObjv != staticListObjv)
        ckfree ((VOID*) listObjv);
}

/*-----------------------------------------------------------------------------
 * TclX_NewKeyedListObj --
 *   Create and initialize a new keyed list object.
 *
 * Returns:
 *    A pointer to the object.
 *-----------------------------------------------------------------------------
 */
Tcl_Obj *
TclX_NewKeyedListObj ()
{
    Tcl_Obj *keylPtr = Tcl_NewObj ();
    keylIntObj_t *keylIntPtr = AllocKeyedListIntRep ();

    keylPtr->internalRep.otherValuePtr = (VOID *) keylIntPtr;
    keylPtr->typePtr = &keyedListType;
    return keylPtr;
}

/*-----------------------------------------------------------------------------
 * TclX_KeyedListGet --
 *   Retrieve a key value from a keyed list.
 *
 * Parameters:
 *   o interp - Error message will be return in result if there is an error.
 *   o keylPtr - Keyed list object to get key from.
 *   o key - The name of the key to extract.  Will recusively process sub-keys
 *     seperated by `.'.
 *   o valueObjPtrPtr - If the key is found, a pointer to the key object
 *     is returned here.  NULL is returned if the key is not present.
 * Returns:
 *   o TCL_OK - If the key value was returned.
 *   o TCL_BREAK - If the key was not found.
 *   o TCL_ERROR - If an error occured.
 *-----------------------------------------------------------------------------
 */
int
TclX_KeyedListGet (interp, keylPtr, key, valuePtrPtr)
    Tcl_Interp *interp;
    Tcl_Obj    *keylPtr;
    char       *key;
    Tcl_Obj   **valuePtrPtr;
{
    keylIntObj_t *keylIntPtr;
    char *nextSubKey;
    int findIdx;

    if (Tcl_ConvertToType (interp, keylPtr, &keyedListType) != TCL_OK)
        return TCL_ERROR;
    keylIntPtr = (keylIntObj_t *) keylPtr->internalRep.otherValuePtr;
    KEYL_REP_ASSERT (keylIntPtr);

    findIdx = FindKeyedListEntry (keylIntPtr, key, NULL, &nextSubKey);

    /*
     * If not found, return status.
     */
    if (findIdx < 0) {
        *valuePtrPtr = NULL;
        return TCL_BREAK;
    }

    /*
     * If we are at the last subkey, return the entry, otherwise recurse
     * down looking for the entry.
     */
    if (nextSubKey == NULL) {
        *valuePtrPtr = keylIntPtr->entries [findIdx].valuePtr;
        return TCL_OK;
    } else {
        return TclX_KeyedListGet (interp, 
                                  keylIntPtr->entries [findIdx].valuePtr,
                                  nextSubKey,
                                  valuePtrPtr);
    }
}

/*-----------------------------------------------------------------------------
 * TclX_KeyedListSet --
 *   Set a key value in keyed list object.
 *
 * Parameters:
 *   o interp - Error message will be return in result object.
 *   o keylPtr - Keyed list object to update.
 *   o key - The name of the key to extract.  Will recusively process
 *     sub-key seperated by `.'.
 *   o valueObjPtr - The value to set for the key.
 * Returns:
 *   TCL_OK or TCL_ERROR.
 *-----------------------------------------------------------------------------
 */
int
TclX_KeyedListSet (interp, keylPtr, key, valuePtr)
    Tcl_Interp *interp;
    Tcl_Obj    *keylPtr;
    char       *key;
    Tcl_Obj    *valuePtr;
{
    keylIntObj_t *keylIntPtr;
    char *nextSubKey;
    int findIdx, keyLen, status;
    Tcl_Obj *newKeylPtr;

    if (Tcl_ConvertToType (interp, keylPtr, &keyedListType) != TCL_OK)
        return TCL_ERROR;
    keylIntPtr = (keylIntObj_t *) keylPtr->internalRep.otherValuePtr;
    KEYL_REP_ASSERT (keylIntPtr);

    findIdx = FindKeyedListEntry (keylIntPtr, key,
                                  &keyLen, &nextSubKey);

    /*
     * If we are at the last subkey, either update or add an entry.
     */
    if (nextSubKey == NULL) {
        if (findIdx < 0) {
            EnsureKeyedListSpace (keylIntPtr, 1);
            findIdx = keylIntPtr->numEntries;
            keylIntPtr->numEntries++;
        } else {
            ckfree (keylIntPtr->entries [findIdx].key);
            Tcl_DecrRefCount (keylIntPtr->entries [findIdx].valuePtr);
        }
        keylIntPtr->entries [findIdx].key =
            (char *) ckalloc (keyLen + 1);
        strncpy (keylIntPtr->entries [findIdx].key, key, keyLen);
        keylIntPtr->entries [findIdx].key [keyLen] = '\0';
        keylIntPtr->entries [findIdx].valuePtr = valuePtr;
        Tcl_IncrRefCount (valuePtr);
        TclX_InvalidateStringRep (keylPtr);

        KEYL_REP_ASSERT (keylIntPtr);
        return TCL_OK;
    }

    /*
     * If we are not at the last subkey, recurse down, creating new
     * entries if neccessary.  If this level key was not found, it
     * means we must build new subtree. Don't insert the new tree until we
     * come back without error.
     */
    if (findIdx >= 0) {
        DupSharedKeyListChild (keylIntPtr, findIdx);
        status =
            TclX_KeyedListSet (interp, 
                               keylIntPtr->entries [findIdx].valuePtr,
                               nextSubKey, valuePtr);
        if (status == TCL_OK) {
            TclX_InvalidateStringRep (keylPtr);
        }

        KEYL_REP_ASSERT (keylIntPtr);
        return status;
    } else {
        newKeylPtr = TclX_NewKeyedListObj ();
        if (TclX_KeyedListSet (interp, newKeylPtr,
                               nextSubKey, valuePtr) != TCL_OK) {
            Tcl_DecrRefCount (newKeylPtr);
            return TCL_ERROR;
        }
        EnsureKeyedListSpace (keylIntPtr, 1);
        findIdx = keylIntPtr->numEntries++;
        keylIntPtr->entries [findIdx].key =
            (char *) ckalloc (keyLen + 1);
        strncpy (keylIntPtr->entries [findIdx].key, key, keyLen);
        keylIntPtr->entries [findIdx].key [keyLen] = '\0';
        keylIntPtr->entries [findIdx].valuePtr = newKeylPtr;
        Tcl_IncrRefCount (newKeylPtr);
        TclX_InvalidateStringRep (keylPtr);

        KEYL_REP_ASSERT (keylIntPtr);
        return TCL_OK;
    }
}

/*-----------------------------------------------------------------------------
 * TclX_KeyedListDelete --
 *   Delete a key value from keyed list.
 *
 * Parameters:
 *   o interp - Error message will be return in result if there is an error.
 *   o keylPtr - Keyed list object to update.
 *   o key - The name of the key to extract.  Will recusively process
 *     sub-key seperated by `.'.
 * Returns:
 *   o TCL_OK - If the key was deleted.
 *   o TCL_BREAK - If the key was not found.
 *   o TCL_ERROR - If an error occured.
 *-----------------------------------------------------------------------------
 */
int
TclX_KeyedListDelete (interp, keylPtr, key)
    Tcl_Interp *interp;
    Tcl_Obj    *keylPtr;
    char       *key;
{
    keylIntObj_t *keylIntPtr, *subKeylIntPtr;
    char *nextSubKey;
    int findIdx, status;

    if (Tcl_ConvertToType (interp, keylPtr, &keyedListType) != TCL_OK)
        return TCL_ERROR;
    keylIntPtr = (keylIntObj_t *) keylPtr->internalRep.otherValuePtr;

    findIdx = FindKeyedListEntry (keylIntPtr, key, NULL, &nextSubKey);

    /*
     * If not found, return status.
     */
    if (findIdx < 0) {
        KEYL_REP_ASSERT (keylIntPtr);
        return TCL_BREAK;
    }

    /*
     * If we are at the last subkey, delete the entry.
     */
    if (nextSubKey == NULL) {
        DeleteKeyedListEntry (keylIntPtr, findIdx);
        TclX_InvalidateStringRep (keylPtr);

        KEYL_REP_ASSERT (keylIntPtr);
        return TCL_OK;
    }

    /*
     * If we are not at the last subkey, recurse down.  If the entry is
     * deleted and the sub-keyed list is empty, delete it as well.  Must
     * invalidate string, as it caches all representations below it.
     */
    DupSharedKeyListChild (keylIntPtr, findIdx);

    status = TclX_KeyedListDelete (interp,
                                   keylIntPtr->entries [findIdx].valuePtr,
                                   nextSubKey);
    if (status == TCL_OK) {
        subKeylIntPtr = (keylIntObj_t *)
            keylIntPtr->entries [findIdx].valuePtr->internalRep.otherValuePtr;
        if (subKeylIntPtr->numEntries == 0) {
            DeleteKeyedListEntry (keylIntPtr, findIdx);
        }
        TclX_InvalidateStringRep (keylPtr);
    }

    KEYL_REP_ASSERT (keylIntPtr);
    return status;
}

/*-----------------------------------------------------------------------------
 * TclX_KeyedListGetKeys --
 *   Retrieve a list of keyed list keys.
 *
 * Parameters:
 *   o interp - Error message will be return in result if there is an error.
 *   o keylPtr - Keyed list object to get key from.
 *   o key - The name of the key to get the sub keys for.  NULL or empty
 *     to retrieve all top level keys.
 *   o listObjPtrPtr - List object is returned here with key as values.
 * Returns:
 *   o TCL_OK - If the zero or more key where returned.
 *   o TCL_BREAK - If the key was not found.
 *   o TCL_ERROR - If an error occured.
 *-----------------------------------------------------------------------------
 */
int
TclX_KeyedListGetKeys (interp, keylPtr, key, listObjPtrPtr)
    Tcl_Interp *interp;
    Tcl_Obj    *keylPtr;
    char       *key;
    Tcl_Obj   **listObjPtrPtr;
{
    keylIntObj_t *keylIntPtr;
    Tcl_Obj *nameObjPtr, *listObjPtr;
    char *nextSubKey;
    int idx, findIdx;

    if (Tcl_ConvertToType (interp, keylPtr, &keyedListType) != TCL_OK)
        return TCL_ERROR;
    keylIntPtr = (keylIntObj_t *) keylPtr->internalRep.otherValuePtr;

    /*
     * If key is not NULL or empty, then recurse down until we go past
     * the end of all of the elements of the key.
     */
    if ((key != NULL) && (key [0] != '\0')) {
        findIdx = FindKeyedListEntry (keylIntPtr, key, NULL, &nextSubKey);
        if (findIdx < 0) {
            assert (keylIntPtr->arraySize >= keylIntPtr->numEntries);  /*FIX:*/
            return TCL_BREAK;
        }
        assert (keylIntPtr->arraySize >= keylIntPtr->numEntries);  /*FIX:*/
        return TclX_KeyedListGetKeys (interp, 
                                      keylIntPtr->entries [findIdx].valuePtr,
                                      nextSubKey,
                                      listObjPtrPtr);
    }

    /*
     * Reached the end of the full key, return all keys at this level.
     */
    listObjPtr = Tcl_NewListObj (0, NULL);
    for (idx = 0; idx < keylIntPtr->numEntries; idx++) {
        nameObjPtr = Tcl_NewStringObj (keylIntPtr->entries [idx].key,
                                       -1);
        if (Tcl_ListObjAppendElement (interp, listObjPtr,
                                      nameObjPtr) != TCL_OK) {
            Tcl_DecrRefCount (nameObjPtr);
            Tcl_DecrRefCount (listObjPtr);
            return TCL_ERROR;
        }
    }
    *listObjPtrPtr = listObjPtr;
    assert (keylIntPtr->arraySize >= keylIntPtr->numEntries);  /*FIX:*/
    return TCL_OK;
}

/*-----------------------------------------------------------------------------
 * Tcl_KeylgetObjCmd --
 *     Implements the TCL keylget command:
 *         keylget listvar ?key? ?retvar | {}?
 *-----------------------------------------------------------------------------
 */
static int
TclX_KeylgetObjCmd (clientData, interp, objc, objv)
    ClientData   clientData;
    Tcl_Interp  *interp;
    int          objc;
    Tcl_Obj     *CONST objv[];
{
    Tcl_Obj *keylPtr, *valuePtr;
    char *key;
    int keyLen, strLen, status;

    if ((objc < 2) || (objc > 4)) {
        return TclX_WrongArgs (interp, objv [0],
                               "listvar ?key? ?retvar | {}?");
    }

    /*
     * Handle request for list of keys, use keylkeys command.
     */
    if (objc == 2)
        return TclX_KeylkeysObjCmd (clientData, interp, objc, objv);

    keylPtr = Tcl_ObjGetVar2 (interp, objv [1], NULL, 
                              TCL_PARSE_PART1 | TCL_LEAVE_ERR_MSG);
    if (keylPtr == NULL) {
        return TCL_ERROR;
    }

    /*
     * Handle retrieving a value for a specified key.
     */
    key = Tcl_GetStringFromObj (objv [2], &keyLen);
    if (ValidateKey (interp, key, keyLen, TRUE) == TCL_ERROR) {
        return TCL_ERROR;
    }

    status = TclX_KeyedListGet (interp, keylPtr, key, &valuePtr);
    if (status == TCL_ERROR)
        return TCL_ERROR;

    /*
     * Handle key not found.
     */
    if (status == TCL_BREAK) {
        if (objc == 3) {
            TclX_StringAppendObjResult (interp, "key \"",  key,
                                        "\" not found in keyed list",
                                        (char *) NULL);
            return TCL_ERROR;
        } else {
            Tcl_SetBooleanObj (Tcl_GetObjResult (interp), FALSE);
            return TCL_OK;
        }
    }

    /*
     * No variable specified, so return value in the result.
     */
    if (objc == 3) {
        Tcl_SetObjResult (interp, valuePtr);
        return TCL_OK;
    }

    /*
     * Variable (or empty variable name) specified.
     */
    if (!TclX_IsNullObj (objv [3])) {
        if (Tcl_ObjSetVar2 (interp, objv [3], NULL, valuePtr,
                            TCL_PARSE_PART1 | TCL_LEAVE_ERR_MSG) == NULL)
            return TCL_ERROR;
    }
    Tcl_SetBooleanObj (Tcl_GetObjResult (interp), TRUE);
    return TCL_OK;
}

/*-----------------------------------------------------------------------------
 * Tcl_KeylsetObjCmd --
 *     Implements the TCL keylset command:
 *         keylset listvar key value ?key value...?
 *-----------------------------------------------------------------------------
 */
static int
TclX_KeylsetObjCmd (clientData, interp, objc, objv)
    ClientData   clientData;
    Tcl_Interp  *interp;
    int          objc;
    Tcl_Obj     *CONST objv[];
{
    Tcl_Obj *keylVarPtr, *keylPtr;
    char *key;
    int idx, keyLen;

    if ((objc < 4) || ((objc % 2) != 0)) {
        return TclX_WrongArgs (interp, objv [0],
                               "listvar key value ?key value...?");
    }

    /*
     * Get the variable that we are going to update.  If the var doesn't exist,
     * create it.  If it is shared by more than being a variable, duplicated
     * it.
     */
    keylVarPtr = Tcl_ObjGetVar2 (interp, objv [1], NULL, TCL_PARSE_PART1);
    if ((keylVarPtr == NULL) || (Tcl_IsShared (keylVarPtr))) {
        if (keylVarPtr == NULL) {
            keylPtr = TclX_NewKeyedListObj ();
        } else {
            keylPtr = Tcl_DuplicateObj (keylVarPtr);
        }
        keylVarPtr = Tcl_ObjSetVar2 (interp, objv [1], NULL, keylPtr,
                                     TCL_PARSE_PART1 | TCL_LEAVE_ERR_MSG);
        if (keylVarPtr == NULL) {
            Tcl_DecrRefCount (keylPtr);
            return TCL_ERROR;
        }
        if (keylVarPtr != keylPtr)
            Tcl_DecrRefCount (keylPtr);
    }
    keylPtr = keylVarPtr;

    for (idx = 2; idx < objc; idx += 2) {
        key = Tcl_GetStringFromObj (objv [idx], &keyLen);
        if (ValidateKey (interp, key, keyLen, TRUE) == TCL_ERROR) {
            return TCL_ERROR;
        }

        if (TclX_KeyedListSet (interp, keylPtr, key, objv [idx+1]) != TCL_OK)
            return TCL_ERROR;
    }

    return TCL_OK;
}

/*-----------------------------------------------------------------------------
 * Tcl_KeyldelObjCmd --
 *     Implements the TCL keyldel command:
 *         keyldel listvar key ?key ...?
 *----------------------------------------------------------------------------
 */
static int
TclX_KeyldelObjCmd (clientData, interp, objc, objv)
    ClientData   clientData;
    Tcl_Interp  *interp;
    int          objc;
    Tcl_Obj     *CONST objv[];
{
    Tcl_Obj *keylVarPtr, *keylPtr;
    char *key;
    int idx, keyLen, status;

    if (objc < 3) {
        return TclX_WrongArgs (interp, objv [0], "listvar key ?key ...?");
    }

    /*
     * Get the variable that we are going to update.  If it is shared by more
     * than being a variable, duplicated it.
     */
    keylVarPtr = Tcl_ObjGetVar2 (interp, objv [1], NULL, 
                                 TCL_PARSE_PART1 | TCL_LEAVE_ERR_MSG);
    if (keylVarPtr == NULL) {
        return TCL_ERROR;
    }
    if (Tcl_IsShared (keylVarPtr)) {
        keylPtr = Tcl_DuplicateObj (keylVarPtr);
        keylVarPtr = Tcl_ObjSetVar2 (interp, objv [1], NULL, keylPtr,
                                     TCL_PARSE_PART1 | TCL_LEAVE_ERR_MSG);
        if (keylVarPtr == NULL) {
            Tcl_DecrRefCount (keylPtr);
            return TCL_ERROR;
        }
        if (keylVarPtr != keylPtr)
            Tcl_DecrRefCount (keylPtr);
    }
    keylPtr = keylVarPtr;

    for (idx = 2; idx < objc; idx++) {
        key = Tcl_GetStringFromObj (objv [idx], &keyLen);
        if (ValidateKey (interp, key, keyLen, TRUE) == TCL_ERROR) {
            return TCL_ERROR;
        }

        status = TclX_KeyedListDelete (interp, keylPtr, key);
        switch (status) {
          case TCL_BREAK:
            TclX_StringAppendObjResult (interp, "key not found: \"",
                                        key, "\"", (char *) NULL);
            return TCL_ERROR;
          case TCL_ERROR:
            return TCL_ERROR;
        }
    }

    return TCL_OK;
}

/*-----------------------------------------------------------------------------
 * Tcl_KeylkeysObjCmd --
 *     Implements the TCL keylkeys command:
 *         keylkeys listvar ?key?
 *-----------------------------------------------------------------------------
 */
static int
TclX_KeylkeysObjCmd (clientData, interp, objc, objv)
    ClientData   clientData;
    Tcl_Interp  *interp;
    int          objc;
    Tcl_Obj     *CONST objv[];
{
    Tcl_Obj *keylPtr, *listObjPtr;
    char *key;
    int keyLen, status;

    if ((objc < 2) || (objc > 3)) {
        return TclX_WrongArgs (interp, objv [0], "listvar ?key?");
    }
    keylPtr = Tcl_ObjGetVar2 (interp, objv [1], NULL, 
                              TCL_PARSE_PART1 | TCL_LEAVE_ERR_MSG);
    if (keylPtr == NULL) {
        return TCL_ERROR;
    }

    /*
     * If key argument is not specified, then objv [2] is NULL or empty,
     * meaning get top level keys.
     */
    if (objc < 3) {
        key = NULL;
    } else {
        key = Tcl_GetStringFromObj (objv [2], &keyLen);
        if (ValidateKey (interp, key, keyLen, TRUE) == TCL_ERROR) {
            return TCL_ERROR;
        }
    }

    status = TclX_KeyedListGetKeys (interp, keylPtr, key, &listObjPtr);
    switch (status) {
      case TCL_BREAK:
        TclX_StringAppendObjResult (interp, "key not found: \"",
                                    key, "\"", (char *) NULL);
        return TCL_ERROR;
      case TCL_ERROR:
        return TCL_ERROR;
    }

    Tcl_SetObjResult (interp, listObjPtr);

    return TCL_OK;
}

/*-----------------------------------------------------------------------------
 * TclX_KeyedListInit --
 *   Initialize the keyed list commands for this interpreter.
 *
 * Parameters:
 *   o interp - Interpreter to add commands to.
 *-----------------------------------------------------------------------------
 */
void
TclX_KeyedListInit (interp)
    Tcl_Interp *interp;
{
    Tcl_RegisterObjType (&keyedListType);

    Tcl_CreateObjCommand (interp, 
			  "keylget",
			  TclX_KeylgetObjCmd,
                          (ClientData) NULL,
			  (Tcl_CmdDeleteProc*) NULL);

    Tcl_CreateObjCommand (interp, 
			  "keylset",
			  TclX_KeylsetObjCmd,
                          (ClientData) NULL,
			  (Tcl_CmdDeleteProc*) NULL);

    Tcl_CreateObjCommand (interp,
			  "keyldel",
			  TclX_KeyldelObjCmd,
                          (ClientData) NULL,
			  (Tcl_CmdDeleteProc*) NULL);

    Tcl_CreateObjCommand (interp, 
			  "keylkeys",
			  TclX_KeylkeysObjCmd,
                          (ClientData) NULL,
			  (Tcl_CmdDeleteProc*) NULL);
}


