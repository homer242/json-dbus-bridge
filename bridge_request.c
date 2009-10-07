/*
 *   json dbus bridge
 *
 *   Copyright (c) 2009 by Michael Olbrich <m.olbrich@pengutronix.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as
 *   published by the Free Software Foundation; either version 2.1 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#define __STRICT_ANSI__
#include <json.h>
#include <dbus/dbus.h>

#include "bridge_request.h"
#include "bridge.h"

int bridge_request_init(bridge_request_t *self, bridge_t *bridge, DBusConnection *dbus_connection, int socket)
{

	if (FCGX_InitRequest(&self->request, socket, FCGI_FAIL_ACCEPT_ON_INTR) != 0) {
		return EINVAL;
	}
	self->tokener = json_tokener_new();
	self->dbus_connection = dbus_connection;
	self->bridge = bridge;
	self->next = 0;
	return 0;
}

int bridge_request_destroy(bridge_request_t *self)
{
	json_tokener_free(self->tokener);
	FCGX_Free(&self->request, TRUE);
	return 0;
}

int bridge_request_accept(bridge_request_t *self)
{
	return FCGX_Accept_r(&self->request);
}

int bridge_request_getinput(bridge_request_t *self, char **data)
{
	char *contentLength;
	char *buffer;
	int len = 0;

	if ((contentLength = FCGX_GetParam("CONTENT_LENGTH", self->request.envp)) != 0)
		len = strtol(contentLength, NULL, 10);

	if (len <= 0)
		return EINVAL;

	if ((buffer = malloc(len)) == 0) {
		FCGX_FPrintF(self->request.err, "out of memory!");
		return ENOMEM;
	}
	if (FCGX_GetStr(buffer, len, self->request.in) != len) {
		FCGX_FPrintF(self->request.err, "Got less data than expected.");
		return EINVAL;
	}
	*data = buffer;
	return 0;
}

void bridge_request_transmit(bridge_request_t *self, struct json_object *obj)
{
	FCGX_FPrintF(self->request.out, "Content-type: application/json\r\n\r\n");
	FCGX_FPrintF(self->request.out, json_object_to_json_string(obj));
}

void bridge_request_fatal(bridge_request_t *self)
{
	FCGX_FPrintF(self->request.out, "Status: 400 Bad Request\r\n\r\n");
}

int bridge_request_create_response(bridge_request_t *self, struct json_object **response,
				   struct json_object *error, struct json_object *result)
{
	struct json_object *obj;

	if (!self)
		return EINVAL;

	if ((obj = json_object_new_object()) == 0)
		return ENOMEM;

	json_object_object_add(obj, "id", json_object_new_int(self->id));
	json_object_object_add(obj, "error", error);
	json_object_object_add(obj, "result", result);

	*response = obj;
	return 0;
}

void bridge_request_json_error(bridge_request_t *self, error_origin_t origin,
			       error_code_t code, const char *message)
{
	struct json_object *response, *error;
	if ((error = json_object_new_object()) == 0)
		goto failed;

	json_object_object_add(error, "origin", json_object_new_int(origin));
	json_object_object_add(error, "code", json_object_new_int(code));
	json_object_object_add(error, "message", json_object_new_string((char *)message));

	if (bridge_request_create_response(self, &response, error, 0) != 0)
		goto failed;

	bridge_request_transmit(self, response);

	json_object_put(response);
	return;
failed:
	bridge_request_fatal(self);
}

void bridge_request_error(bridge_request_t *self, const char *message)
{
	bridge_request_json_error(self, error_origin_server,
				  error_code_illegal_service, message);
}

int _json_object_object_getint(struct json_object *obj, char *key, int *value)
{
	struct json_object *tmp;

	if (((tmp = json_object_object_get(obj, key)) == 0) ||
	    !json_object_is_type(tmp, json_type_int))
		return EINVAL;
	*value = json_object_get_int(tmp);
	return 0;
}

int _json_object_object_getstring(struct json_object *obj, char *key, const char **value)
{
	struct json_object *tmp;

	if (((tmp = json_object_object_get(obj, key)) == 0) ||
	    !json_object_is_type(tmp, json_type_string))
		return EINVAL;
	*value = json_object_get_string(tmp);
	return 0;
}

int bridge_request_dbus_params_basic(bridge_request_t *self,
				     struct json_object *param, int type,
				     DBusMessageIter *it)
{
	int int_value;
	//int64_t ll_value;
	double double_value;
	const char *str_value;
	void *value = 0;

	switch (type) {
		case DBUS_TYPE_BOOLEAN:
			if (json_object_get_type(param) != json_type_boolean) {
				bridge_request_error(self,
					"boolean value expected.");
				return EINVAL;
			}
			int_value = json_object_get_boolean(param);
			value = &int_value;
			break;
		case DBUS_TYPE_DOUBLE:
			if (json_object_get_type(param) != json_type_double) {
				bridge_request_error(self,
					"double value expected.");
				return EINVAL;
			}
			double_value = json_object_get_double(param);
			value = &double_value;
			break;
		case DBUS_TYPE_BYTE:
		case DBUS_TYPE_INT16:
		case DBUS_TYPE_UINT16:
		case DBUS_TYPE_INT32:
		case DBUS_TYPE_UINT32:
		case DBUS_TYPE_INT64:
		case DBUS_TYPE_UINT64:
			if (json_object_get_type(param) != json_type_int) {
				bridge_request_error(self,
					"integer value expected.");
				return EINVAL;
			}
			int_value = json_object_get_int(param);
			value = &int_value;
			break;
		case DBUS_TYPE_STRING:
		case DBUS_TYPE_OBJECT_PATH:
		case DBUS_TYPE_SIGNATURE:
			if (json_object_get_type(param) != json_type_string) {
				bridge_request_error(self,
					"string value expected.");
				return EINVAL;
			}
			str_value = json_object_get_string(param);
			value = &str_value;
			break;
		default:
			return 0;
	}
	if (!dbus_message_iter_append_basic(it, type, value)) {
		bridge_request_error(self,
			"out of memory.");
		return ENOMEM;
	}
	return 0;
}

int bridge_request_dbus_params_element(bridge_request_t *self,
				       struct json_object *element,
				       DBusSignatureIter *sigIt,
				       DBusMessageIter *it);

int bridge_request_dbus_params_dict(bridge_request_t *self,
				    struct json_object *element,
				    DBusSignatureIter *sigIt,
				    DBusMessageIter *it)
{
	DBusMessageIter args;
	DBusSignatureIter sigArgs;
	int ret;

	dbus_signature_iter_recurse(sigIt, &sigArgs);

	if (dbus_signature_iter_get_current_type(&sigArgs) != DBUS_TYPE_STRING) {
		bridge_request_error(self,
			"string dict key type expected.");
		return EINVAL;
	}
	dbus_signature_iter_next(&sigArgs);

	if (json_object_get_type(element) != json_type_object) {
		bridge_request_error(self,
			"object expected.");
		return EINVAL;
	}
	json_object_object_foreach(element, key, tmp) {
		DBusSignatureIter tmpSigArgs = sigArgs;
		dbus_message_iter_open_container(it, DBUS_TYPE_DICT_ENTRY, 0,&args);
		dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &key);
		
		ret = bridge_request_dbus_params_element(self,
			tmp, &tmpSigArgs, &args);
		if (ret != 0)
			return ret;
		dbus_message_iter_close_container(it, &args);
	}
	return 0;
}

int bridge_request_dbus_params_array(bridge_request_t *self,
				     struct json_object *params,
				     int idx, const char *sig,
				     DBusMessageIter *it);

int bridge_request_dbus_params_element(bridge_request_t *self,
				       struct json_object *element,
				       DBusSignatureIter *sigIt,
				       DBusMessageIter *it)
{
	int type;
	int ret;

	type = dbus_signature_iter_get_current_type(sigIt);

	if (dbus_type_is_basic(type)) {
		if ((ret = bridge_request_dbus_params_basic(self,
				 element, type, it)) != 0) {
			return ret;
		}
	}
	else if (type == DBUS_TYPE_VARIANT) {
		struct json_object *tmp;
		DBusMessageIter args;
		const char *vSig;

		if (json_object_get_type(element) != json_type_array) {
			bridge_request_error(self,
				"array expected.");
			return EINVAL;
		}
		tmp = json_object_array_get_idx(element, 0);
		if (!tmp) {
			bridge_request_error(self,
				"variant signature expected.");
			return EINVAL;
		}
		if (json_object_get_type(tmp) != json_type_string) {
			bridge_request_error(self,
				"variant signature expected.");
			return EINVAL;
		}
		vSig = json_object_get_string(tmp);
		dbus_message_iter_open_container(it, type,
			 vSig, &args);
		ret = bridge_request_dbus_params_array(self,
			element, 1, vSig, &args);
		if (ret != 0)
			return EINVAL;
		dbus_message_iter_close_container(it, &args);
	}
	else if (type == DBUS_TYPE_ARRAY) {
		DBusMessageIter args;
		DBusSignatureIter sigArgs;
		int cType;
		char *cSig;

		dbus_signature_iter_recurse(sigIt, &sigArgs);
		cType = dbus_signature_iter_get_current_type(&sigArgs);
		cSig = dbus_signature_iter_get_signature(&sigArgs);

		dbus_message_iter_open_container(it, type, cSig, &args);
		dbus_free(cSig);

		if (cType == DBUS_TYPE_DICT_ENTRY) {
			ret = bridge_request_dbus_params_dict(self,
				element, &sigArgs, &args);
			if (ret != 0)
				return ret;
		}
		else {
			int i, len;

			if (json_object_get_type(element) != json_type_array) {
				bridge_request_error(self,
					"array expected.");
				return EINVAL;
			}
			len = json_object_array_length(element);
			for (i = 0; i < len; ++i) {
				struct json_object *tmp;
				DBusSignatureIter tmpSigArgs = sigArgs;

				tmp = json_object_array_get_idx(element, i);
				if (!tmp) {
					bridge_request_error(self,
						"value expected.");
					return EINVAL;
				}
				ret = bridge_request_dbus_params_element(self,
					tmp, &tmpSigArgs, &args);
				if (ret != 0)
					return ret;
			}
		}
		dbus_message_iter_close_container(it, &args);
	}
	else {
		bridge_request_error(self,
			"unsupported json argument type.");
		return EINVAL;
	}
	return 0;
}

int bridge_request_dbus_params_array(bridge_request_t *self,
				     struct json_object *params,
				     int idx, const char *sig,
				     DBusMessageIter *it)
{
	DBusSignatureIter sigIt;
	struct json_object *element;
	int i, ret, len;

	if (!dbus_signature_validate(sig, 0)) {
		bridge_request_error(self,
			"invalid argument signature.");
		return EINVAL;
	}
	dbus_signature_iter_init(&sigIt, sig);

	len = json_object_array_length(params);

	for (i = idx; i < len; ++i) {
		element = json_object_array_get_idx(params, i);
		if (!element) {
			bridge_request_error(self,
				"value expected.");
			return EINVAL;
		}
		ret = bridge_request_dbus_params_element(self,
			element, &sigIt, it);
		if (ret != 0)
			return ret;
		if (!dbus_signature_iter_next(&sigIt))
			break;
	}
	return 0;
}

int bridge_request_dbus_params(bridge_request_t *self,
			       struct json_object *params,
			       DBusMessageIter *it)
{
	struct json_object *tmp;
	const char *sig;

	/*
	 * expect [ "<signature>", <data> ]
	 */
	if (json_object_get_type(params) != json_type_array) {
		bridge_request_error(self,
			"array expected.");
		return EINVAL;
	}
	if (json_object_array_length(params) == 0)
		return 0;
	tmp = json_object_array_get_idx(params, 0);
	if (!tmp) {
		bridge_request_error(self,
			"signature string expected.");
		return EINVAL;
	}
	if (json_object_get_type(tmp) != json_type_string) {
		bridge_request_error(self,
			"First Argument must be the signature string.");
		return EINVAL;
	}
	sig = json_object_get_string(tmp);

	return bridge_request_dbus_params_array(self, params, 1, sig, it);
}

int bridge_request_json_params(bridge_request_t *self, DBusMessageIter *it,
			       struct json_object **result);

int bridge_request_json_params_parse(bridge_request_t *self, DBusMessageIter *it,
				     struct json_object **result, const char **key)
{
	if (key)
		*key = 0;
	*result = 0;
	switch (dbus_message_iter_get_arg_type(it)) {
		case DBUS_TYPE_STRING:
		case DBUS_TYPE_SIGNATURE:
		case DBUS_TYPE_OBJECT_PATH: {
			char *value;
			dbus_message_iter_get_basic(it, &value);
			*result = json_object_new_string(value);
			break;
		}
		case DBUS_TYPE_INT16:
		case DBUS_TYPE_UINT16:
		case DBUS_TYPE_INT32:
		case DBUS_TYPE_UINT32:
		case DBUS_TYPE_BYTE: {
			int value;
			dbus_message_iter_get_basic(it, &value);
			*result = json_object_new_int(value);
			break;
		}
		case DBUS_TYPE_DOUBLE: {
			double value;
			dbus_message_iter_get_basic(it, &value);
			*result = json_object_new_double(value);
			break;
		}
		case DBUS_TYPE_BOOLEAN: {
			int value;
			dbus_message_iter_get_basic(it, &value);
			*result = json_object_new_boolean(value);
			break;
		}
		case DBUS_TYPE_ARRAY:
		case DBUS_TYPE_VARIANT: {
			DBusMessageIter args;
			dbus_message_iter_recurse(it, &args);
			bridge_request_json_params(self, &args, result);
			break;
		}
		case DBUS_TYPE_DICT_ENTRY: {
			DBusMessageIter args;
			if (!key)
				break;
			dbus_message_iter_recurse(it, &args);
			if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
				bridge_request_error(self,
					"dictionary with non-string keys not supported.");
				break;
			}
			dbus_message_iter_get_basic(&args, key);
			if (!dbus_message_iter_has_next(&args))
				break;
			dbus_message_iter_next(&args);
			bridge_request_json_params_parse(self, &args, result, 0);
			break;
		}
		default:
			break;
	}
	return *result ? 0 : EINVAL;
}

int bridge_request_json_params(bridge_request_t *self, DBusMessageIter *it,
			       struct json_object **result)
{
	struct json_object *tmp;
	const char *key;
	int is_array = 0;

	*result = 0;
	is_array = dbus_message_iter_has_next(it);

	do {
		bridge_request_json_params_parse(self, it, &tmp, &key);
		if (!tmp) {
			bridge_request_error(self ,
				"unsupported dbus argument type.");
			FCGX_FPrintF(self->request.err, "type: %c\n", dbus_message_iter_get_arg_type(it));
			return EINVAL;
		}

		if (key != 0) {
			if (!*result)
				*result = json_object_new_object();
			json_object_object_add(*result, key, tmp);
		}
		else if (is_array) {
			if (!*result)
				*result = json_object_new_array();
			json_object_array_add(*result, tmp);
		}
		else
			*result = tmp;
	} while (dbus_message_iter_next(it));

	return 0;
}

int bridge_request_to_dbus(bridge_request_t *self, struct json_object *in_json,
			   DBusMessage **out_dbus)
{
	DBusMessageIter it;
	struct json_object *params;
	const char *service, *iface;
	char *method, *path;
	int ret;

	if (!json_object_is_type(in_json, json_type_object) ||
	    (_json_object_object_getint(in_json, "id", &self->id) != 0)) {
		bridge_request_fatal(self);
		return EINVAL;
	}
	if ((ret = _json_object_object_getstring(in_json, "service", &service) != 0)) {
		bridge_request_error(self, "No service specified.");
		return ret;
	}
	if ((ret = _json_object_object_getstring(in_json, "method", &iface) != 0)) {
		bridge_request_error(self, "No method specified.");
		return ret;
	}
	if ((path = strchr(service, '|')) == 0) {
		bridge_request_error(self,
			"Service must be '<dbus service>|<dbus path>'.");
		return EINVAL;
	}
	*path = '\0';
	++path;
	if ((method = strrchr(iface, '.')) == 0) {
		bridge_request_error(self,
			"Method must be '<interface>.<method>'.");
		return EINVAL;
	}
	*method = '\0';
	++method;
	if (((params = json_object_object_get(in_json, "params")) == 0) ||
	    !json_object_is_type(params, json_type_array)) {
		bridge_request_error(self,
			"'param' arg missing or not an array.");
		return EINVAL;
	}

	*out_dbus = dbus_message_new_method_call(service, path, iface, method);
	if (out_dbus == NULL) {
		bridge_request_error(self, "Out of memory.");
		return ENOMEM;
	}
	dbus_message_iter_init_append(*out_dbus, &it);
	if ((ret = bridge_request_dbus_params(self, params, &it)) != 0)
		dbus_message_unref(*out_dbus);

	return ret;
}

int bridge_request_call_dbus_json(bridge_request_t *self, DBusMessage *in_dbus)
{
	DBusMessageIter it;
	struct json_object *result;
	struct json_object *out_json;
	int ret;

	if (dbus_message_iter_init(in_dbus, &it)) {
		if ((ret = bridge_request_json_params(self, &it, &result)) != 0)
			goto finish;
	}
	else
		result = 0;

	if ((ret = bridge_request_create_response(self, &out_json, 0, result)) != 0) {
		bridge_request_error(self, "Out of memory.");
	}
	else {
		bridge_request_transmit(self, out_json);
		json_object_put(out_json);
	}

finish:
	dbus_message_unref(in_dbus);

	FCGX_Finish_r(&self->request);
	self->next = self->bridge->head;
	self->bridge->head = self;
	return 0;
}

void _dbus_reply_notify_handler(DBusPendingCall *pending, void *user_data)
{
	bridge_request_t *self = (bridge_request_t*)user_data;

	bridge_request_call_dbus_json(self, dbus_pending_call_steal_reply(pending));
	dbus_pending_call_unref(pending);
}

int bridge_request_call_json_dbus(bridge_request_t *self, struct json_object *in_json)
{
	int ret;
	DBusMessage *msg;
	DBusPendingCall *pending;

	if ((ret = bridge_request_to_dbus(self, in_json, &msg)) != 0)
		goto send_fail;

	if (!dbus_connection_send_with_reply(self->dbus_connection, msg,
				 &pending, -1)) {
		bridge_request_error(self, "Out of memory.");
		ret = ENOMEM;
		goto send_fail;
	}
	if (pending) {
		dbus_pending_call_set_notify(pending, _dbus_reply_notify_handler,
			 self, 0);
	}
send_fail:
	dbus_message_unref(msg);
	return ret;
}

int bridge_request_handle(bridge_request_t *self)
{
	int ret;
	char *buffer;
	struct json_object *in_json;


	if ((ret = bridge_request_getinput(self, &buffer)) != 0) {
		bridge_request_fatal(self);
		return 0;
	}
	in_json = json_tokener_parse_ex(self->tokener, buffer, -1);
	if (!in_json) {
		bridge_request_fatal(self);
		goto out;
	}

	bridge_request_call_json_dbus(self, in_json);

out:
	json_object_put(in_json);
	json_tokener_reset(self->tokener);
	free(buffer);
	return 0;
}

