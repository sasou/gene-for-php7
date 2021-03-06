/*
 +----------------------------------------------------------------------+
 | gene                                                                 |
 +----------------------------------------------------------------------+
 | This source file is subject to version 3.01 of the PHP license,      |
 | that is bundled with this package in the file LICENSE, and is        |
 | available through the world-wide-web at the following url:           |
 | http://www.php.net/license/3_01.txt                                  |
 | If you did not receive a copy of the PHP license and are unable to   |
 | obtain it through the world-wide-web, please send a note to          |
 | license@php.net so we can mail you a copy immediately.               |
 +----------------------------------------------------------------------+
 | Author: Sasou  <admin@php-gene.com> web:www.php-gene.com             |
 +----------------------------------------------------------------------+
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "main/SAPI.h"
#include "Zend/zend_API.h"
#include "zend_exceptions.h"
#include "zend_string.h"
#include "zend_globals.h"
#include "zend_virtual_cwd.h"
#include "zend_smart_str.h"
#include "main/php_output.h"
#include "zend_execute.h"
#include "Zend/zend_interfaces.h" /* for zend_class_serialize_deny */

#include "../gene.h"
#include "../factory/load.h"
#include "../config/configs.h"
#include "../common/common.h"

zend_class_entry * gene_load_ce;

ZEND_BEGIN_ARG_INFO_EX(gene_load_void_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(gene_load_arg_import, 0, 0, 1)
	ZEND_ARG_INFO(0, file)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(gene_load_arg_autoload, 0, 0, 1)
	ZEND_ARG_INFO(0, className)
ZEND_END_ARG_INFO()


int exec_by_symbol_table(zval *obj, zend_op_array *op_array, zend_array *symbol_table, zval *result) /* {{{ */ {
	zend_execute_data *call;
	uint32_t call_info;

	op_array->scope = Z_OBJCE_P(obj);

	zend_function *func = (zend_function *)op_array;

#if PHP_VERSION_ID >= 70400
	call_info = ZEND_CALL_HAS_THIS | ZEND_CALL_NESTED_CODE | ZEND_CALL_HAS_SYMBOL_TABLE;
#elif PHP_VERSION_ID >= 70100
	call_info = ZEND_CALL_NESTED_CODE | ZEND_CALL_HAS_SYMBOL_TABLE;
#else
	call_info = ZEND_CALL_NESTED_CODE;
#endif

#if PHP_VERSION_ID < 70400
	call = zend_vm_stack_push_call_frame(call_info, func, 0, op_array->scope, Z_OBJ_P(obj));
#else
    call = zend_vm_stack_push_call_frame(call_info, func, 0, Z_OBJ_P(obj));
#endif

	call->symbol_table = symbol_table;

	zend_init_execute_data(call, op_array, result);

	ZEND_ADD_CALL_FLAG(call, ZEND_CALL_TOP);
	zend_execute_ex(call);
	zend_vm_stack_free_call_frame(call);
	return 1;
}
/* }}} */

/** {{{ int gene_load_import(char *path)
 */
int gene_load_import(char *path , zval *obj, zend_array *symbol_table) {
	zend_file_handle file_handle;
	zend_op_array *op_array;
	zend_stat_t sb;

	if (UNEXPECTED(VCWD_STAT(path, &sb) == -1)) {
		return 0;
	}

#if PHP_VERSION_ID < 70400
	file_handle.filename = path;
	file_handle.free_filename = 0;
	file_handle.type = ZEND_HANDLE_FILENAME;
	file_handle.opened_path = NULL;
	file_handle.handle.fp = NULL;
#else
	/* setup file-handle */
	zend_stream_init_filename(&file_handle, path);
#endif


	op_array = zend_compile_file(&file_handle, ZEND_INCLUDE);

	if (op_array && file_handle.handle.stream.handle) {
		if (!file_handle.opened_path) {
			file_handle.opened_path = zend_string_init(path, strlen(path), 0);
		}

		zend_hash_add_empty_element(&EG(included_files),
				file_handle.opened_path);
	}
	zend_destroy_file_handle(&file_handle);

	if (op_array) {
		zval result;
		ZVAL_UNDEF(&result);

		if (obj && symbol_table) {
			exec_by_symbol_table(obj, op_array, symbol_table, &result);
		} else {
			zend_execute(op_array, &result);
		}

		destroy_op_array(op_array);
		efree_size(op_array, sizeof(op_array));
		if (!EG(exception)) {
			zval_ptr_dtor(&result);
		}
		return 1;
	}
	return 0;
}
/* }}} */

void gene_load_file_by_class_name (char *className) {
	char *fileNmae = NULL, *filePath = NULL, *class_lowercase = NULL;
	zend_class_entry *ce 	= NULL;

	fileNmae = estrdup(className);

	if (GENE_G(use_namespace)) {
		replaceAll(fileNmae, '\\', '/');
	} else {
		replaceAll(fileNmae, '_', '/');
	}

	if (GENE_G(directory)) {
		spprintf(&filePath, 0, "%s/%s.php", GENE_G(app_root), fileNmae);
	} else {
		spprintf(&filePath, 0, "%s.php", fileNmae);
	}
	if (!gene_load_import(filePath, NULL, NULL)) {
		if (GENE_G(use_library)) {
			efree(filePath);
			spprintf(&filePath, 0, "%s/%s.php", GENE_G(library_root), fileNmae);
			gene_load_import(filePath, NULL, NULL);
		}
	}
	efree(filePath);
	efree(fileNmae);
}

/*
 *  {{{ zval *gene_load_instance(zval *this_ptr)
 */
zval *gene_load_instance(zval *this_ptr) {
	zval *instance = zend_read_static_property(gene_load_ce,
	GENE_LOAD_PROPERTY_INSTANCE, strlen(GENE_LOAD_PROPERTY_INSTANCE), 1);

	if (Z_TYPE_P(instance) == IS_OBJECT) {
		return instance;
	}
	if (this_ptr) {
		instance = this_ptr;
	} else {
		object_init_ex(instance, gene_load_ce);
	}
	zend_update_static_property(gene_load_ce, GENE_LOAD_PROPERTY_INSTANCE,
			strlen(GENE_LOAD_PROPERTY_INSTANCE), instance);
	return instance;
}
/* }}} */

/** {{{ int gene_loader_register(zval *loader,char *methodName)
 */
int gene_loader_register() {
	zval autoload, function, ret;

	if (GENE_G(auto_load_fun)) {
		ZVAL_STRING(&autoload, GENE_G(auto_load_fun));
	} else {
		if (GENE_G(use_namespace)) {
			ZVAL_STRING(&autoload, GENE_AUTOLOAD_FUNC_NAME_NS);
		} else {
			ZVAL_STRING(&autoload, GENE_AUTOLOAD_FUNC_NAME);
		}
	}
	ZVAL_STRING(&function, GENE_SPL_AUTOLOAD_REGISTER_NAME);
	do {
		zend_fcall_info fci = {
			sizeof(fci),
#if PHP_VERSION_ID < 70100
			EG(function_table),
#endif
			function,
#if PHP_VERSION_ID < 70100
			NULL,
#endif
			&ret,
			&autoload,
			NULL,
			1,
#if PHP_VERSION_ID < 80000
            1
#else
           NULL
#endif
		};


		if (zend_call_function(&fci, NULL) == FAILURE) {
			zval_ptr_dtor(&function);
			zval_ptr_dtor(&autoload);
			zval_ptr_dtor(&ret);
			php_error_docref(NULL, E_WARNING, "Unable to register autoload function %s", GENE_AUTOLOAD_FUNC_NAME);
			return 0;
		}

		zval_ptr_dtor(&function);
		zval_ptr_dtor(&autoload);
		zval_ptr_dtor(&ret);
	} while (0);
	return 1;
}
/* }}} */

/*
 * {{{ gene_load
 */
PHP_METHOD(gene_load, __construct) {

}
/* }}} */

/*
 * {{{ public gene_load::autoload($key)
 */
PHP_METHOD(gene_load, autoload) {
	zend_string *className;
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "S", &className) == FAILURE) {
		return;
	}
	gene_load_file_by_class_name(ZSTR_VAL(className));
}
/* }}} */

/*
 * {{{ public gene_load::load($key)
 */
PHP_METHOD(gene_load, import) {
	zval *self = getThis();
	zend_string *php_script;
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "|S", &php_script) == FAILURE) {
		return;
	}
	if (php_script && ZSTR_LEN(php_script)) {
		if(!gene_load_import(ZSTR_VAL(php_script), NULL, NULL)) {
			php_error_docref(NULL, E_WARNING, "Unable to load file %s", ZSTR_VAL(php_script));
		}
	}
	RETURN_ZVAL(self, 1, 0);
}
/* }}} */

/*
 *  {{{ public gene_reg::getInstance(void)
 */
PHP_METHOD(gene_load, getInstance) {
	zval *load = gene_load_instance(NULL);
	RETURN_ZVAL(load, 1, 0);
}
/* }}} */

/*
 * {{{ gene_load_methods
 */
zend_function_entry gene_load_methods[] = {
	PHP_ME(gene_load, __construct, gene_load_void_arginfo, ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
	PHP_ME(gene_load, getInstance, gene_load_void_arginfo, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	PHP_ME(gene_load, import, gene_load_arg_import, ZEND_ACC_PUBLIC)
	PHP_ME(gene_load, autoload, gene_load_arg_autoload, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	{ NULL, NULL, NULL }
};
/* }}} */

/*
 * {{{ GENE_MINIT_FUNCTION
 */
GENE_MINIT_FUNCTION(load) {
	zend_class_entry gene_load;
	GENE_INIT_CLASS_ENTRY(gene_load, "Gene_Load", "Gene\\Load", gene_load_methods);
	gene_load_ce = zend_register_internal_class_ex(&gene_load, NULL);
	gene_load_ce->ce_flags |= ZEND_ACC_FINAL;
	//static
	zend_declare_property_null(gene_load_ce, GENE_LOAD_PROPERTY_INSTANCE, strlen(GENE_LOAD_PROPERTY_INSTANCE), ZEND_ACC_PROTECTED | ZEND_ACC_STATIC);

	return SUCCESS; // @suppress("Symbol is not resolved")
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
