
/* Module:          results.c
 *
 * Description:     This module contains functions related to 
 *                  retrieving result information through the ODBC API.
 *
 * Classes:         n/a
 *
 * API functions:   SQLRowCount, SQLNumResultCols, SQLDescribeCol, SQLColAttributes,
 *                  SQLGetData, SQLFetch, SQLExtendedFetch, 
 *                  SQLMoreResults(NI), SQLSetPos, SQLSetScrollOptions(NI),
 *                  SQLSetCursorName, SQLGetCursorName
 *
 * Comments:        See "notice.txt" for copyright and license information.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "psqlodbc.h"
#include "dlg_specific.h"
#include "environ.h"
#include "connection.h"
#include "statement.h"
#include "bind.h"
#include "qresult.h"
#include "convert.h"
#include "pgtypes.h" 

#include <stdio.h>

#ifndef WIN32
#include "iodbc.h"
#include "isqlext.h"
#else
#include <windows.h>
#include <sqlext.h>
#endif

extern GLOBAL_VALUES globals;



RETCODE SQL_API SQLRowCount(
        HSTMT      hstmt,
        SDWORD FAR *pcrow)
{
static char *func="SQLRowCount";
StatementClass *stmt = (StatementClass *) hstmt;
QResultClass *res;
char *msg, *ptr;

	if ( ! stmt) {
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}
	if (stmt->manual_result) {
		if (pcrow)
			*pcrow = -1;
		return SQL_SUCCESS;
	}

	if(stmt->statement_type == STMT_TYPE_SELECT) {
		if (stmt->status == STMT_FINISHED) {
			res = SC_get_Result(stmt);

			if(res && pcrow) {
				*pcrow = globals.use_declarefetch ? -1 : QR_get_num_tuples(res);
				return SQL_SUCCESS;
			}
		}
	} else {

		res = SC_get_Result(stmt);
		if (res && pcrow) {
			msg = QR_get_command(res);
			mylog("*** msg = '%s'\n", msg);
			trim(msg);	//	get rid of trailing spaces
			ptr = strrchr(msg, ' ');
			if (ptr) {
				*pcrow = atoi(ptr+1);
				mylog("**** SQLRowCount(): THE ROWS: *pcrow = %d\n", *pcrow);
			}
			else {
				*pcrow = -1;

				mylog("**** SQLRowCount(): NO ROWS: *pcrow = %d\n", *pcrow);
			}

		return SQL_SUCCESS;
		}
	}

	SC_log_error(func, "Bad return value", stmt);
	return SQL_ERROR;     
}


//      This returns the number of columns associated with the database
//      attached to "hstmt".


RETCODE SQL_API SQLNumResultCols(
        HSTMT     hstmt,
        SWORD FAR *pccol)
{       
static char *func="SQLNumResultCols";
StatementClass *stmt = (StatementClass *) hstmt;
QResultClass *result;
char parse_ok;

	if ( ! stmt) {
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	SC_clear_error(stmt);    

	parse_ok = FALSE;
	if (globals.parse && stmt->statement_type == STMT_TYPE_SELECT) {

		if (stmt->parse_status == STMT_PARSE_NONE) {
			mylog("SQLNumResultCols: calling parse_statement on stmt=%u\n", stmt);
			parse_statement(stmt);
		}

		if (stmt->parse_status != STMT_PARSE_FATAL) {
			parse_ok = TRUE;
			*pccol = stmt->nfld;
			mylog("PARSE: SQLNumResultCols: *pccol = %d\n", *pccol);
		}
	}

	if ( ! parse_ok) {

		SC_pre_execute(stmt);       
		result = SC_get_Result(stmt);

		mylog("SQLNumResultCols: result = %u, status = %d, numcols = %d\n", result, stmt->status, result != NULL ? QR_NumResultCols(result) : -1);
		if (( ! result) || ((stmt->status != STMT_FINISHED) && (stmt->status != STMT_PREMATURE)) ) {
			/* no query has been executed on this statement */
			stmt->errornumber = STMT_SEQUENCE_ERROR;
			stmt->errormsg = "No query has been executed with that handle";
			SC_log_error(func, "", stmt);
			return SQL_ERROR;
		}

		*pccol = QR_NumResultCols(result);
	}

	return SQL_SUCCESS;
}


//      -       -       -       -       -       -       -       -       -



//      Return information about the database column the user wants
//      information about.
RETCODE SQL_API SQLDescribeCol(
        HSTMT      hstmt,
        UWORD      icol,
        UCHAR  FAR *szColName,
        SWORD      cbColNameMax,
        SWORD  FAR *pcbColName,
        SWORD  FAR *pfSqlType,
        UDWORD FAR *pcbColDef,
        SWORD  FAR *pibScale,
        SWORD  FAR *pfNullable)
{
static char *func="SQLDescribeCol";
    /* gets all the information about a specific column */
StatementClass *stmt = (StatementClass *) hstmt;
QResultClass *result;
char *col_name = NULL;
Int4 fieldtype = 0;
int precision = 0;
ConnInfo *ci;
char parse_ok;
char buf[255];

	mylog("%s: entering...\n", func);

    if ( ! stmt) {
		SC_log_error(func, "", NULL);
        return SQL_INVALID_HANDLE;
	}

	ci = &(stmt->hdbc->connInfo);

    SC_clear_error(stmt);

	/*	Dont check for bookmark column.  This is the responsibility
		of the driver manager.  
	*/

	icol--;		/* use zero based column numbers */


	parse_ok = FALSE;
	if (globals.parse && stmt->statement_type == STMT_TYPE_SELECT) {

		if (stmt->parse_status == STMT_PARSE_NONE) {
			mylog("SQLDescribeCol: calling parse_statement on stmt=%u\n", stmt);
			parse_statement(stmt);
		}


		mylog("PARSE: DescribeCol: icol=%d, stmt=%u, stmt->nfld=%d, stmt->fi=%u\n", icol, stmt, stmt->nfld, stmt->fi);

		if (stmt->parse_status != STMT_PARSE_FATAL && stmt->fi && stmt->fi[icol]) {

			if (icol >= stmt->nfld) {
				stmt->errornumber = STMT_INVALID_COLUMN_NUMBER_ERROR;
				stmt->errormsg = "Invalid column number in DescribeCol.";
				SC_log_error(func, "", stmt);
				return SQL_ERROR;
			}
			mylog("DescribeCol: getting info for icol=%d\n", icol);

			fieldtype = stmt->fi[icol]->type;
			col_name = stmt->fi[icol]->name;
			precision = stmt->fi[icol]->precision;

			mylog("PARSE: fieldtype=%d, col_name='%s', precision=%d\n", fieldtype, col_name, precision);
			if (fieldtype > 0)
				parse_ok = TRUE;
		}
	}


	/*	If couldn't parse it OR the field being described was not parsed (i.e., because
		it was a function or expression, etc, then do it the old fashioned way.
	*/
	if ( ! parse_ok) {
		SC_pre_execute(stmt);
	
		result = SC_get_Result(stmt);

		mylog("**** SQLDescribeCol: result = %u, stmt->status = %d, !finished=%d, !premature=%d\n", result, stmt->status, stmt->status != STMT_FINISHED, stmt->status != STMT_PREMATURE);
		if ( (NULL == result) || ((stmt->status != STMT_FINISHED) && (stmt->status != STMT_PREMATURE))) {
			/* no query has been executed on this statement */
			stmt->errornumber = STMT_SEQUENCE_ERROR;
			stmt->errormsg = "No query has been assigned to this statement.";
			SC_log_error(func, "", stmt);
			return SQL_ERROR;
		}

		if (icol >= QR_NumResultCols(result)) {
			stmt->errornumber = STMT_INVALID_COLUMN_NUMBER_ERROR;
			stmt->errormsg = "Invalid column number in DescribeCol.";
			sprintf(buf, "Col#=%d, #Cols=%d", icol, QR_NumResultCols(result));
			SC_log_error(func, buf, stmt);
			return SQL_ERROR;
		}

		col_name = QR_get_fieldname(result, icol);
        fieldtype = QR_get_field_type(result, icol);

		precision = pgtype_precision(stmt, fieldtype, icol, globals.unknown_sizes);  // atoi(ci->unknown_sizes)
	}

	mylog("describeCol: col %d fieldname = '%s'\n", icol, col_name);
	mylog("describeCol: col %d fieldtype = %d\n", icol, fieldtype);
	mylog("describeCol: col %d precision = %d\n", icol, precision);

    if (cbColNameMax >= 1) {
        if (pcbColName)  {
            if (col_name) 
                *pcbColName = strlen(col_name);
            else
                *pcbColName = 0;
        }
        if (szColName) {
            if (col_name) 
                strncpy_null(szColName, col_name, cbColNameMax);
            else
                szColName[0] = '\0';
        }
    }


    if (pfSqlType) {
        *pfSqlType = pgtype_to_sqltype(stmt, fieldtype);

		mylog("describeCol: col %d *pfSqlType = %d\n", icol, *pfSqlType);
	}

    if (pcbColDef) {

		if ( precision < 0)
			precision = 0;		// "I dont know"

		*pcbColDef = precision;

		mylog("describeCol: col %d  *pcbColDef = %d\n", icol, *pcbColDef);
	}

    if (pibScale) {
        Int2 scale;
        scale = pgtype_scale(stmt, fieldtype);
        if(scale == -1) { scale = 0; }
        
        *pibScale = scale;
		mylog("describeCol: col %d  *pibScale = %d\n", icol, *pibScale);
    }

    if (pfNullable) {
		if (parse_ok)
			*pfNullable = stmt->fi[icol]->nullable;
		else
			*pfNullable = pgtype_nullable(stmt, fieldtype);

		mylog("describeCol: col %d  *pfNullable = %d\n", icol, *pfNullable);
    }

    return SQL_SUCCESS;
}

//      Returns result column descriptor information for a result set.

RETCODE SQL_API SQLColAttributes(
        HSTMT      hstmt,
        UWORD      icol,
        UWORD      fDescType,
        PTR        rgbDesc,
        SWORD      cbDescMax,
        SWORD  FAR *pcbDesc,
        SDWORD FAR *pfDesc)
{
static char *func = "SQLColAttributes";
StatementClass *stmt = (StatementClass *) hstmt;
char *value;
Int4 field_type = 0;
ConnInfo *ci;
int unknown_sizes;
int cols = 0;
char parse_ok;

	mylog("%s: entering...\n", func);

	if( ! stmt) {
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	ci = &(stmt->hdbc->connInfo);

	/*	Dont check for bookmark column.  This is the responsibility
		of the driver manager.  For certain types of arguments, the column
		number is ignored anyway, so it may be 0.
	*/

	icol--;

	unknown_sizes = globals.unknown_sizes;          // atoi(ci->unknown_sizes);
	if (unknown_sizes == UNKNOWNS_AS_DONTKNOW)		// not appropriate for SQLColAttributes()
		unknown_sizes = UNKNOWNS_AS_MAX;

	parse_ok = FALSE;
	if (globals.parse && stmt->statement_type == STMT_TYPE_SELECT) {

		if (stmt->parse_status == STMT_PARSE_NONE) {
			mylog("SQLColAttributes: calling parse_statement\n");
			parse_statement(stmt);
		}

		cols = stmt->nfld;

		/*	Column Count is a special case.  The Column number is ignored
			in this case.
		*/
		if (fDescType == SQL_COLUMN_COUNT) {
			if (pfDesc)
				*pfDesc = cols;

			return SQL_SUCCESS;
		}

		if (stmt->parse_status != STMT_PARSE_FATAL && stmt->fi && stmt->fi[icol]) {

			if (icol >= cols) {
				stmt->errornumber = STMT_INVALID_COLUMN_NUMBER_ERROR;
				stmt->errormsg = "Invalid column number in DescribeCol.";
				SC_log_error(func, "", stmt);
				return SQL_ERROR;
			}

			field_type = stmt->fi[icol]->type;
			if (field_type > 0)
				parse_ok = TRUE;
		}
	}

	if ( ! parse_ok) {
		SC_pre_execute(stmt);       

		mylog("**** SQLColAtt: result = %u, status = %d, numcols = %d\n", stmt->result, stmt->status, stmt->result != NULL ? QR_NumResultCols(stmt->result) : -1);

		if ( (NULL == stmt->result) || ((stmt->status != STMT_FINISHED) && (stmt->status != STMT_PREMATURE)) ) {
			stmt->errormsg = "Can't get column attributes: no result found.";
			stmt->errornumber = STMT_SEQUENCE_ERROR;
			SC_log_error(func, "", stmt);
			return SQL_ERROR;
		}

		cols = QR_NumResultCols(stmt->result);

		/*	Column Count is a special case.  The Column number is ignored
			in this case.
		*/
		if (fDescType == SQL_COLUMN_COUNT) {
			if (pfDesc)
				*pfDesc = cols;

			return SQL_SUCCESS;
		}

		if (icol >= cols) {
			stmt->errornumber = STMT_INVALID_COLUMN_NUMBER_ERROR;
			stmt->errormsg = "Invalid column number in DescribeCol.";
			SC_log_error(func, "", stmt);
			return SQL_ERROR;
		}

		field_type = QR_get_field_type(stmt->result, icol);
	}

	mylog("colAttr: col %d field_type = %d\n", icol, field_type);

	switch(fDescType) {
	case SQL_COLUMN_AUTO_INCREMENT:
		if (pfDesc) {
			*pfDesc = pgtype_auto_increment(stmt, field_type);
			if (*pfDesc == -1)  /*  non-numeric becomes FALSE (ODBC Doc) */
				*pfDesc = FALSE;
				
		}
		break;

	case SQL_COLUMN_CASE_SENSITIVE:
		if (pfDesc)    
			*pfDesc = pgtype_case_sensitive(stmt, field_type);
		break;

	/* 	This special case is handled above.

	case SQL_COLUMN_COUNT: 
	*/

    case SQL_COLUMN_DISPLAY_SIZE:
		if (pfDesc) {
			if (parse_ok)
				*pfDesc = stmt->fi[icol]->display_size;
			else
				*pfDesc = pgtype_display_size(stmt, field_type, icol, unknown_sizes);
		}

		mylog("SQLColAttributes: col %d, display_size= %d\n", icol, *pfDesc);

        break;

	case SQL_COLUMN_LABEL:
		if (parse_ok && stmt->fi[icol]->alias[0] != '\0') {
			strncpy_null((char *)rgbDesc, stmt->fi[icol]->alias, cbDescMax);
			if (pcbDesc)
				*pcbDesc = strlen(stmt->fi[icol]->alias);

			break;

			mylog("SQLColAttr: COLUMN_LABEL = '%s'\n", rgbDesc);
		}	// otherwise same as column name

	case SQL_COLUMN_NAME:

		if (parse_ok)
			value = stmt->fi[icol]->name;
		else
			value = QR_get_fieldname(stmt->result, icol);

		strncpy_null((char *)rgbDesc, value, cbDescMax);

		if (pcbDesc)
			*pcbDesc = strlen(value);

		mylog("SQLColAttr: COLUMN_NAME = '%s'\n", rgbDesc);
		break;

	case SQL_COLUMN_LENGTH:
		if (pfDesc) {
			if (parse_ok)
				*pfDesc = stmt->fi[icol]->length;
			else
				*pfDesc = pgtype_length(stmt, field_type, icol, unknown_sizes); 
		}
		mylog("SQLColAttributes: col %d, length = %d\n", icol, *pfDesc);
        break;

	case SQL_COLUMN_MONEY:
		if (pfDesc)    
			*pfDesc = pgtype_money(stmt, field_type);
		break;

	case SQL_COLUMN_NULLABLE:
		if (pfDesc) {
			if (parse_ok)
				*pfDesc = stmt->fi[icol]->nullable;
			else
				*pfDesc = pgtype_nullable(stmt, field_type);
		}
		break;

	case SQL_COLUMN_OWNER_NAME:
		strncpy_null((char *)rgbDesc, "", cbDescMax);
		if (pcbDesc)        
			*pcbDesc = 0;
		break;

	case SQL_COLUMN_PRECISION:
		if (pfDesc) {
			if (parse_ok)
				*pfDesc = stmt->fi[icol]->precision;
			else
				*pfDesc = pgtype_precision(stmt, field_type, icol, unknown_sizes);
		}
		mylog("SQLColAttributes: col %d, precision = %d\n", icol, *pfDesc);
        break;

	case SQL_COLUMN_QUALIFIER_NAME:
		strncpy_null((char *)rgbDesc, "", cbDescMax);
		if (pcbDesc)        
			*pcbDesc = 0;
		break;

	case SQL_COLUMN_SCALE:
		if (pfDesc)    
			*pfDesc = pgtype_scale(stmt, field_type);
		break;

	case SQL_COLUMN_SEARCHABLE:
		if (pfDesc)    
			*pfDesc = pgtype_searchable(stmt, field_type);
		break;

    case SQL_COLUMN_TABLE_NAME:
		if (parse_ok && stmt->fi[icol]->ti) {
			strncpy_null((char *)rgbDesc, stmt->fi[icol]->ti->name, cbDescMax);
			if (pcbDesc)        
				*pcbDesc = strlen(stmt->fi[icol]->ti->name);
		}
		else {
			strncpy_null((char *)rgbDesc, "", cbDescMax);
			if (pcbDesc)        
				*pcbDesc = 0;
		}

		mylog("SQLColAttr: TABLE_NAME = '%s'\n", rgbDesc);
        break;

	case SQL_COLUMN_TYPE:
		if (pfDesc) {
			*pfDesc = pgtype_to_sqltype(stmt, field_type);
		}
		break;

	case SQL_COLUMN_TYPE_NAME:
		value = pgtype_to_name(stmt, field_type);
		strncpy_null((char *)rgbDesc, value, cbDescMax);
		if (pcbDesc)        
			*pcbDesc = strlen(value);
		break;

	case SQL_COLUMN_UNSIGNED:
		if (pfDesc) {
			*pfDesc = pgtype_unsigned(stmt, field_type);
			if(*pfDesc == -1)	/* non-numeric becomes TRUE (ODBC Doc) */
				*pfDesc = TRUE;
		}
		break;

	case SQL_COLUMN_UPDATABLE:
		if (pfDesc)    {
			/*	Neither Access or Borland care about this.

			if (field_type == PG_TYPE_OID)
				*pfDesc = SQL_ATTR_READONLY;
			else
			*/

			*pfDesc = SQL_ATTR_WRITE;
			mylog("SQLColAttr: UPDATEABLE = %d\n", *pfDesc);
		}
		break;
    }

    return SQL_SUCCESS;
}

//      Returns result data for a single column in the current row.

RETCODE SQL_API SQLGetData(
        HSTMT      hstmt,
        UWORD      icol,
        SWORD      fCType,
        PTR        rgbValue,
        SDWORD     cbValueMax,
        SDWORD FAR *pcbValue)
{
static char *func="SQLGetData";
QResultClass *res;
StatementClass *stmt = (StatementClass *) hstmt;
int num_cols, num_rows;
Int4 field_type;
void *value;
int result;


mylog("SQLGetData: enter, stmt=%u\n", stmt);

    if( ! stmt) {
		SC_log_error(func, "", NULL);
        return SQL_INVALID_HANDLE;
    }
	res = stmt->result;

    if (STMT_EXECUTING == stmt->status) {
        stmt->errormsg = "Can't get data while statement is still executing.";
        stmt->errornumber = STMT_SEQUENCE_ERROR;
		SC_log_error(func, "", stmt);
        return SQL_ERROR;
    }

    if (stmt->status != STMT_FINISHED) {
        stmt->errornumber = STMT_STATUS_ERROR;
        stmt->errormsg = "GetData can only be called after the successful execution on a SQL statement";
		SC_log_error(func, "", stmt);
        return SQL_ERROR;
    }

    if (icol == 0) {
        stmt->errormsg = "Bookmarks are not currently supported.";
        stmt->errornumber = STMT_NOT_IMPLEMENTED_ERROR;
		SC_log_error(func, "", stmt);
        return SQL_ERROR;
    }

    // use zero-based column numbers
    icol--;

    // make sure the column number is valid
    num_cols = QR_NumResultCols(res);
    if (icol >= num_cols) {
        stmt->errormsg = "Invalid column number.";
        stmt->errornumber = STMT_INVALID_COLUMN_NUMBER_ERROR;
		SC_log_error(func, "", stmt);
        return SQL_ERROR;
    }

	if ( stmt->manual_result || ! globals.use_declarefetch) {
		// make sure we're positioned on a valid row
		num_rows = QR_get_num_tuples(res);
		if((stmt->currTuple < 0) ||
		   (stmt->currTuple >= num_rows)) {
			stmt->errormsg = "Not positioned on a valid row for GetData.";
			stmt->errornumber = STMT_INVALID_CURSOR_STATE_ERROR;
			SC_log_error(func, "", stmt);
			return SQL_ERROR;
		}
		mylog("     num_rows = %d\n", num_rows);
		if ( stmt->manual_result) {
			value = QR_get_value_manual(res, stmt->currTuple, icol);
		}
		else {
			value = QR_get_value_backend_row(res, stmt->currTuple, icol);
		}
		mylog("     value = '%s'\n", value);
	}
	else { /* its a SOCKET result (backend data) */
		if (stmt->currTuple == -1 || ! res || ! res->tupleField) {
			stmt->errormsg = "Not positioned on a valid row for GetData.";
			stmt->errornumber = STMT_INVALID_CURSOR_STATE_ERROR;
			SC_log_error(func, "", stmt);
			return SQL_ERROR;
		}

		value = QR_get_value_backend(res, icol);

		mylog("  socket: value = '%s'\n", value);
	}

	field_type = QR_get_field_type(res, icol);

	mylog("**** SQLGetData: icol = %d, fCType = %d, field_type = %d, value = '%s'\n", icol, fCType, field_type, value);

	stmt->current_col = icol;

    result = copy_and_convert_field(stmt, field_type, value, 
                                    fCType, rgbValue, cbValueMax, pcbValue);

	stmt->current_col = -1;

	switch(result) {
	case COPY_OK:
		return SQL_SUCCESS;

	case COPY_UNSUPPORTED_TYPE:
		stmt->errormsg = "Received an unsupported type from Postgres.";
		stmt->errornumber = STMT_RESTRICTED_DATA_TYPE_ERROR;
		SC_log_error(func, "", stmt);
		return SQL_ERROR;

	case COPY_UNSUPPORTED_CONVERSION:
		stmt->errormsg = "Couldn't handle the necessary data type conversion.";
		stmt->errornumber = STMT_RESTRICTED_DATA_TYPE_ERROR;
		SC_log_error(func, "", stmt);
		return SQL_ERROR;

	case COPY_RESULT_TRUNCATED:
		stmt->errornumber = STMT_TRUNCATED;
		stmt->errormsg = "The buffer was too small for the result.";
		return SQL_SUCCESS_WITH_INFO;

	case COPY_GENERAL_ERROR:	/* error msg already filled in */
		SC_log_error(func, "", stmt);
		return SQL_ERROR;

	case COPY_NO_DATA_FOUND:
		/* SC_log_error(func, "no data found", stmt); */
		return SQL_NO_DATA_FOUND;

    default:
        stmt->errormsg = "Unrecognized return value from copy_and_convert_field.";
        stmt->errornumber = STMT_INTERNAL_ERROR;
		SC_log_error(func, "", stmt);
        return SQL_ERROR;
    }
}

RETCODE
SC_fetch(StatementClass *stmt)
{
static char *func = "SC_fetch";
QResultClass *res = stmt->result;
int retval, result;
Int2 num_cols, lf;
Oid type;
char *value;
ColumnInfoClass *ci;
// TupleField *tupleField;

	stmt->last_fetch_count = 0;
	ci = QR_get_fields(res);		/* the column info */

	mylog("manual_result = %d, use_declarefetch = %d\n", stmt->manual_result, globals.use_declarefetch);
 
	if ( stmt->manual_result || ! globals.use_declarefetch) {

		if (stmt->currTuple >= QR_get_num_tuples(res) -1 || 
			(stmt->options.maxRows > 0 && stmt->currTuple == stmt->options.maxRows - 1)) {

			/*	if at the end of the tuples, return "no data found" 
				and set the cursor past the end of the result set 
			*/
			stmt->currTuple = QR_get_num_tuples(res);	
			return SQL_NO_DATA_FOUND;
		}
 
		mylog("**** SQLFetch: manual_result\n");
		(stmt->currTuple)++;
	}
	else {

		// read from the cache or the physical next tuple
		retval = QR_next_tuple(res);
		if (retval < 0) {
			mylog("**** SQLFetch: end_tuples\n");
			return SQL_NO_DATA_FOUND;
		}
		else if (retval > 0)
			(stmt->currTuple)++;		// all is well

		else {
			mylog("SQLFetch: error\n");
			stmt->errornumber = STMT_EXEC_ERROR;
			stmt->errormsg = "Error fetching next row";
			SC_log_error(func, "", stmt);
			return SQL_ERROR;
		}
	}

	num_cols = QR_NumResultCols(res);

	result = SQL_SUCCESS;
	stmt->last_fetch_count = 1;

	for (lf=0; lf < num_cols; lf++) {

		mylog("fetch: cols=%d, lf=%d, stmt = %u, stmt->bindings = %u, buffer[] = %u\n", num_cols, lf, stmt, stmt->bindings, stmt->bindings[lf].buffer);

		/*	reset for SQLGetData */
		stmt->bindings[lf].data_left = -1;

		if (stmt->bindings[lf].buffer != NULL) {
            // this column has a binding

            // type = QR_get_field_type(res, lf);
			type = CI_get_oid(ci, lf);		/* speed things up */

			mylog("type = %d\n", type);

			if (stmt->manual_result) {
				value = QR_get_value_manual(res, stmt->currTuple, lf);
				mylog("manual_result\n");
			}
			else if (globals.use_declarefetch)
				value = QR_get_value_backend(res, lf);
			else {
				value = QR_get_value_backend_row(res, stmt->currTuple, lf);
			}

			mylog("value = '%s'\n",  (value==NULL)?"<NULL>":value);

			retval = copy_and_convert_field_bindinfo(stmt, type, value, lf);

			mylog("copy_and_convert: retval = %d\n", retval);

			switch(retval) {
			case COPY_OK:
				break;	/*	OK, do next bound column */

			case COPY_UNSUPPORTED_TYPE:
				stmt->errormsg = "Received an unsupported type from Postgres.";
				stmt->errornumber = STMT_RESTRICTED_DATA_TYPE_ERROR;
				SC_log_error(func, "", stmt);
				result = SQL_ERROR;
				break;

			case COPY_UNSUPPORTED_CONVERSION:
				stmt->errormsg = "Couldn't handle the necessary data type conversion.";
				stmt->errornumber = STMT_RESTRICTED_DATA_TYPE_ERROR;
				SC_log_error(func, "", stmt);
				result = SQL_ERROR;
				break;

			case COPY_RESULT_TRUNCATED:
				stmt->errornumber = STMT_TRUNCATED;
				stmt->errormsg = "The buffer was too small for the result.";
				result = SQL_SUCCESS_WITH_INFO;
				break;

			case COPY_GENERAL_ERROR:	/* error msg already filled in */
				SC_log_error(func, "", stmt);
				result = SQL_ERROR;
				break;

			/*  This would not be meaningful in SQLFetch. */
			case COPY_NO_DATA_FOUND:
				break;

			default:
				stmt->errormsg = "Unrecognized return value from copy_and_convert_field.";
				stmt->errornumber = STMT_INTERNAL_ERROR;
				SC_log_error(func, "", stmt);
				result = SQL_ERROR;
				break;
			}
		}
	}

	return result;
}


//      Returns data for bound columns in the current row ("hstmt->iCursor"),
//      advances the cursor.

RETCODE SQL_API SQLFetch(
        HSTMT   hstmt)
{
static char *func = "SQLFetch";
StatementClass *stmt = (StatementClass *) hstmt;   
QResultClass *res;

mylog("SQLFetch: stmt = %u, stmt->result= %u\n", stmt, stmt->result);

	if ( ! stmt) {
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	SC_clear_error(stmt);

	if ( ! (res = stmt->result)) {
		stmt->errormsg = "Null statement result in SQLFetch.";
		stmt->errornumber = STMT_SEQUENCE_ERROR;
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}

	if (stmt->status == STMT_EXECUTING) {
		stmt->errormsg = "Can't fetch while statement is still executing.";
		stmt->errornumber = STMT_SEQUENCE_ERROR;
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}


	if (stmt->status != STMT_FINISHED) {
		stmt->errornumber = STMT_STATUS_ERROR;
		stmt->errormsg = "Fetch can only be called after the successful execution on a SQL statement";
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}

	if (stmt->bindings == NULL) {
		// just to avoid a crash if the user insists on calling this
		// function even if SQL_ExecDirect has reported an Error
		stmt->errormsg = "Bindings were not allocated properly.";
		stmt->errornumber = STMT_SEQUENCE_ERROR;
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}

	QR_set_rowset_size(res, 1);
	QR_inc_base(res, stmt->last_fetch_count);	

	return SC_fetch(stmt);
}

//      This fetchs a block of data (rowset).

RETCODE SQL_API SQLExtendedFetch(
        HSTMT      hstmt,
        UWORD      fFetchType,
        SDWORD     irow,
        UDWORD FAR *pcrow,
        UWORD  FAR *rgfRowStatus)
{
static char *func = "SQLExtendedFetch";
StatementClass *stmt = (StatementClass *) hstmt;
QResultClass *res;
int num_tuples, i, save_rowset_size;
RETCODE result;
char truncated, error;

mylog("SQLExtendedFetch: stmt=%u\n", stmt);

	if ( ! stmt) {
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	if ( globals.use_declarefetch && ! stmt->manual_result) {
		if ( fFetchType != SQL_FETCH_NEXT) {
			stmt->errornumber = STMT_NOT_IMPLEMENTED_ERROR;
			stmt->errormsg = "Unsupported fetch type for SQLExtendedFetch with UseDeclareFetch option.";
			return SQL_ERROR;
		}
	}

	SC_clear_error(stmt);

	if ( ! (res = stmt->result)) {
		stmt->errormsg = "Null statement result in SQLExtendedFetch.";
		stmt->errornumber = STMT_SEQUENCE_ERROR;
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}

	if (stmt->status == STMT_EXECUTING) {
		stmt->errormsg = "Can't fetch while statement is still executing.";
		stmt->errornumber = STMT_SEQUENCE_ERROR;
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}

	if (stmt->status != STMT_FINISHED) {
		stmt->errornumber = STMT_STATUS_ERROR;
		stmt->errormsg = "ExtendedFetch can only be called after the successful execution on a SQL statement";
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}

	if (stmt->bindings == NULL) {
		// just to avoid a crash if the user insists on calling this
		// function even if SQL_ExecDirect has reported an Error
		stmt->errormsg = "Bindings were not allocated properly.";
		stmt->errornumber = STMT_SEQUENCE_ERROR;
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}

	/*	Initialize to no rows fetched */
	if (rgfRowStatus)
		for (i = 0; i < stmt->options.rowset_size; i++)
			*(rgfRowStatus + i) = SQL_ROW_NOROW;

	if (pcrow)
		*pcrow = 0;

	num_tuples = QR_get_num_tuples(res);

	/*	Save and discard the saved rowset size */
	save_rowset_size = stmt->save_rowset_size;
	stmt->save_rowset_size = -1;

	switch (fFetchType)  {
	case SQL_FETCH_NEXT:

		/*	From the odbc spec... If positioned before the start of the RESULT SET,
			then this should be equivalent to SQL_FETCH_FIRST.
		*/

		if (stmt->rowset_start < 0)
			stmt->rowset_start = 0;

		else {
			
			stmt->rowset_start += (save_rowset_size > 0 ? save_rowset_size : stmt->options.rowset_size);
		}

		mylog("SQL_FETCH_NEXT: num_tuples=%d, currtuple=%d\n", num_tuples, stmt->currTuple);
		break;

	case SQL_FETCH_PRIOR:
		mylog("SQL_FETCH_PRIOR: num_tuples=%d, currtuple=%d\n", num_tuples, stmt->currTuple);


		/*	From the odbc spec... If positioned after the end of the RESULT SET,
			then this should be equivalent to SQL_FETCH_LAST.
		*/

		if (stmt->rowset_start >= num_tuples) {
			stmt->rowset_start = num_tuples <= 0 ? 0 : (num_tuples - stmt->options.rowset_size);

		}
		else {

			stmt->rowset_start -= stmt->options.rowset_size;

		}

		break;

	case SQL_FETCH_FIRST:
		mylog("SQL_FETCH_FIRST: num_tuples=%d, currtuple=%d\n", num_tuples, stmt->currTuple);

		stmt->rowset_start = 0;
		break;

	case SQL_FETCH_LAST:
		mylog("SQL_FETCH_LAST: num_tuples=%d, currtuple=%d\n", num_tuples, stmt->currTuple);

		stmt->rowset_start = num_tuples <= 0 ? 0 : (num_tuples - stmt->options.rowset_size) ;
		break;

	case SQL_FETCH_ABSOLUTE:
		mylog("SQL_FETCH_ABSOLUTE: num_tuples=%d, currtuple=%d, irow=%d\n", num_tuples, stmt->currTuple, irow);

		/*	Position before result set, but dont fetch anything */
		if (irow == 0) {
			stmt->rowset_start = -1;
			stmt->currTuple = -1;
			return SQL_NO_DATA_FOUND;
		}
		/*	Position before the desired row */
		else if (irow > 0) {
			stmt->rowset_start = irow - 1;
		}
		/*	Position with respect to the end of the result set */
		else {
			stmt->rowset_start = num_tuples + irow;
		}    

		break;

	case SQL_FETCH_RELATIVE:
		
		/*	Refresh the current rowset -- not currently implemented, but lie anyway */
		if (irow == 0) {
			break;
		}

		stmt->rowset_start += irow;

		
		break;

	default:
		SC_log_error(func, "Unsupported SQLExtendedFetch Direction", stmt);
		return SQL_ERROR;   

	}           


	/***********************************/
	/*	CHECK FOR PROPER CURSOR STATE  */
	/***********************************/
	/*	Handle Declare Fetch style specially because the end is not really the end... */
	if ( globals.use_declarefetch && ! stmt->manual_result) {
		if (QR_end_tuples(res)) {
			return SQL_NO_DATA_FOUND;
		}
	}
	else {
		/*	If *new* rowset is after the result_set, return no data found */
		if (stmt->rowset_start >= num_tuples) {
			stmt->rowset_start = num_tuples;
			return SQL_NO_DATA_FOUND;
		}
	}

	/*	If *new* rowset is prior to result_set, return no data found */
	if (stmt->rowset_start < 0) {
		if (stmt->rowset_start + stmt->options.rowset_size <= 0) {
			stmt->rowset_start = -1;
			return SQL_NO_DATA_FOUND;
		}
		else {	/*	overlap with beginning of result set, so get first rowset */
			stmt->rowset_start = 0;
		}
	}

	/*	currTuple is always 1 row prior to the rowset */
	stmt->currTuple = stmt->rowset_start - 1;

	/*	increment the base row in the tuple cache */
	QR_set_rowset_size(res, stmt->options.rowset_size);
	QR_inc_base(res, stmt->last_fetch_count);	
		
	/*	Physical Row advancement occurs for each row fetched below */

	mylog("SQLExtendedFetch: new currTuple = %d\n", stmt->currTuple);

	truncated = error = FALSE;
	for (i = 0; i < stmt->options.rowset_size; i++) {

		stmt->bind_row = i;		// set the binding location
		result = SC_fetch(stmt);

		/*	Determine Function status */
		if (result == SQL_NO_DATA_FOUND)
			break;
		else if (result == SQL_SUCCESS_WITH_INFO)
			truncated = TRUE;
		else if (result == SQL_ERROR)
			error = TRUE;

		/*	Determine Row Status */
		if (rgfRowStatus) {
			if (result == SQL_ERROR) 
				*(rgfRowStatus + i) = SQL_ROW_ERROR;
			else
				*(rgfRowStatus + i)= SQL_ROW_SUCCESS;
		}
	}

	/*	Save the fetch count for SQLSetPos */
	stmt->last_fetch_count= i;

	/*	Reset next binding row */
	stmt->bind_row = 0;

	/*	Move the cursor position to the first row in the result set. */
	stmt->currTuple = stmt->rowset_start;

	/*	For declare/fetch, need to reset cursor to beginning of rowset */
	if (globals.use_declarefetch && ! stmt->manual_result) {
		QR_set_position(res, 0);
	}

	/*	Set the number of rows retrieved */
	if (pcrow)
		*pcrow = i;

	if (i == 0)
		return SQL_NO_DATA_FOUND;		/*	Only DeclareFetch should wind up here */
	else if (error)
		return SQL_ERROR;
	else if (truncated)
		return SQL_SUCCESS_WITH_INFO;
	else
		return SQL_SUCCESS;

}


//      This determines whether there are more results sets available for
//      the "hstmt".

/* CC: return SQL_NO_DATA_FOUND since we do not support multiple result sets */
RETCODE SQL_API SQLMoreResults(
        HSTMT   hstmt)
{
	return SQL_NO_DATA_FOUND;
}

//     This positions the cursor within a rowset, that was positioned using SQLExtendedFetch.
//	   This will be useful (so far) only when using SQLGetData after SQLExtendedFetch.	
RETCODE SQL_API SQLSetPos(
        HSTMT   hstmt,
        UWORD   irow,
        UWORD   fOption,
        UWORD   fLock)
{
static char *func = "SQLSetPos";
StatementClass *stmt = (StatementClass *) hstmt;
QResultClass *res;
int num_cols, i;
BindInfoClass *bindings = stmt->bindings;

	if ( ! stmt) {
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	if (fOption != SQL_POSITION && fOption != SQL_REFRESH) {
		stmt->errornumber = STMT_NOT_IMPLEMENTED_ERROR;
		stmt->errormsg = "Only SQL_POSITION/REFRESH is supported for SQLSetPos";
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}

	if ( ! (res = stmt->result)) {
		stmt->errormsg = "Null statement result in SQLSetPos.";
		stmt->errornumber = STMT_SEQUENCE_ERROR;
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}
	num_cols = QR_NumResultCols(res);

	if (irow == 0) {
		stmt->errornumber = STMT_ROW_OUT_OF_RANGE;
		stmt->errormsg = "Driver does not support Bulk operations.";
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}

	if (irow > stmt->last_fetch_count) {
		stmt->errornumber = STMT_ROW_OUT_OF_RANGE;
		stmt->errormsg = "Row value out of range";
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}

	irow--;

	/*	Reset for SQLGetData */
	for (i = 0; i < num_cols; i++)
		bindings[i].data_left = -1;

	QR_set_position(res, irow);

	stmt->currTuple = stmt->rowset_start + irow;

	return SQL_SUCCESS;

}

//      Sets options that control the behavior of cursors.

RETCODE SQL_API SQLSetScrollOptions(
        HSTMT      hstmt,
        UWORD      fConcurrency,
        SDWORD  crowKeyset,
        UWORD      crowRowset)
{
static char *func = "SQLSetScrollOptions";

	SC_log_error(func, "Function not implemented", (StatementClass *) hstmt);
	return SQL_ERROR;
}


//      Set the cursor name on a statement handle

RETCODE SQL_API SQLSetCursorName(
        HSTMT     hstmt,
        UCHAR FAR *szCursor,
        SWORD     cbCursor)
{
static char *func="SQLSetCursorName";
StatementClass *stmt = (StatementClass *) hstmt;
int len;

mylog("SQLSetCursorName: hstmt=%u, szCursor=%u, cbCursorMax=%d\n", hstmt, szCursor, cbCursor);

	if ( ! stmt) {
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	len = (cbCursor == SQL_NTS) ? strlen(szCursor) : cbCursor;
	mylog("cursor len = %d\n", len);
	if (len <= 0 || len > sizeof(stmt->cursor_name) - 1) {
		stmt->errornumber = STMT_INVALID_CURSOR_NAME;
		stmt->errormsg = "Invalid Cursor Name";
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}
	strncpy_null(stmt->cursor_name, szCursor, cbCursor);
	return SQL_SUCCESS;
}

//      Return the cursor name for a statement handle

RETCODE SQL_API SQLGetCursorName(
        HSTMT     hstmt,
        UCHAR FAR *szCursor,
        SWORD     cbCursorMax,
        SWORD FAR *pcbCursor)
{
static char *func="SQLGetCursorName";
StatementClass *stmt = (StatementClass *) hstmt;

mylog("SQLGetCursorName: hstmt=%u, szCursor=%u, cbCursorMax=%d, pcbCursor=%u\n", hstmt, szCursor, cbCursorMax, pcbCursor);

	if ( ! stmt) {
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}


	if ( stmt->cursor_name[0] == '\0') {
		stmt->errornumber = STMT_NO_CURSOR_NAME;
		stmt->errormsg = "No Cursor name available";
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}

	strncpy_null(szCursor, stmt->cursor_name, cbCursorMax);

	if (pcbCursor)
		*pcbCursor = strlen(szCursor);

	return SQL_SUCCESS;
}


