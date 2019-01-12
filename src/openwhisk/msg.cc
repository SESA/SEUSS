// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <string>
#include <stdio.h>
#include <yajl/yajl_tree.h>

#include <sstream> /* std::ostringstream */

#include "msg.h"

using namespace std;

openwhisk::msg::CompletionMessage::CompletionMessage( openwhisk::msg::ActivationMessage am ){
  transid_ = am.transid_;
  invoker_ = am.rootControllerIndex_; 
  response_.activationId_ = am.activationId_;
  response_.name_ = am.action_.name_;
  response_.namespace_ = am.user_.namespace_.name_;
  response_.subject_ = am.user_.subject_;
  response_.version_ = am.action_.version_;
}

openwhisk::msg::ActivationMessage::ActivationMessage( string json ){
  const char *a_path[] = {"activationId", (const char *)0};
  const char *c_path[] = {"content", (const char *)0};
  const char *r_path[] = {"revision", (const char *)0};
  const char *t_path[] = {"transid", (const char *)0};
  const char *ac_path_1[] = {"action", "path", (const char *)0};
  const char *ac_path_2[] = {"action", "name", (const char *)0};
  const char *ac_path_3[] = {"action", "version", (const char *)0};
  const char *inst_path_1[] = {"rootControllerIndex", "instance", (const char *)0};
  const char *inst_path_2[] = {"rootControllerIndex", "name", (const char *)0};
  const char *user_path_1[] = {"user", "subject", (const char *)0};
  const char *user_path_2[] = {"user", "authkey", (const char *)0};
  const char *user_path_3[] = {"user", "namespace", "name", (const char *)0};
  const char *user_path_4[] = {"user", "namespace", "uuid", (const char *)0};
  char errbuf[1024];
  yajl_val yv;

  /** Generator for content string */
#if 0
  yajl_gen g;
  g = yajl_gen_alloc(NULL);
#endif
  /** parse json, errors to stderr */
  auto yajl_node =
      yajl_tree_parse(json.c_str(), errbuf, json.size());
  if (yajl_node == NULL) {
    fprintf(stderr, "ActivationMessage json parse_error: %s\n", errbuf);
    return;
  }
  // activationId
  yv = yajl_tree_get(yajl_node, a_path, yajl_t_string);
  if (yv)
    activationId_ = YAJL_GET_STRING(yv);

  // content (input arguments)
  yv = yajl_tree_get(yajl_node, c_path, yajl_t_object);
  if (YAJL_IS_OBJECT(yv)) {
    auto c_obj = YAJL_GET_OBJECT(yv);
    std::ostringstream ss;
    ss << "{";
    for (uint16_t i = 0; i < c_obj->len; i++) {
      if (i > 0)
        ss << ",";
      ss << "\"" << c_obj->keys[i] << "\":";
      if (YAJL_IS_STRING(c_obj->values[i])) {
        ss << "\"" << YAJL_GET_STRING(c_obj->values[i]) << "\"";
      } else {
        ss << YAJL_GET_INTEGER(c_obj->values[i]);
      }
    }
    ss << "}";
    content_ = ss.str();
  } else {
    content_ = "{}";
  }

  // revision
  yv = yajl_tree_get(yajl_node, r_path, yajl_t_string);
  if (yv)
    revision_ = YAJL_GET_STRING(yv);
	// transid (array)
  yv = yajl_tree_get(yajl_node, t_path, yajl_t_array);
  if (YAJL_IS_ARRAY(yv)){
		auto yarr = YAJL_GET_ARRAY(yv);
		transid_.name_ = YAJL_GET_STRING(yarr->values[0]);
		transid_.id_ = YAJL_GET_INTEGER(yarr->values[1]);
  } 
	// action/path 
  yv = yajl_tree_get(yajl_node, ac_path_1, yajl_t_string);
  if (yv)
    action_.path_ = YAJL_GET_STRING(yv);
	// action/name 
  yv = yajl_tree_get(yajl_node, ac_path_2, yajl_t_string);
  if (yv)
    action_.name_ = YAJL_GET_STRING(yv);
	// action/version 
  yv = yajl_tree_get(yajl_node, ac_path_3, yajl_t_string);
  if (yv)
    action_.version_ = YAJL_GET_STRING(yv);
	// rootControllerIndex
  yv = yajl_tree_get(yajl_node, inst_path_1, yajl_t_number);
  if (yv)
    rootControllerIndex_.instance_ = YAJL_GET_INTEGER(yv);
  yv = yajl_tree_get(yajl_node, inst_path_2, yajl_t_string);
  if (yv)
    rootControllerIndex_.name_ = YAJL_GET_STRING(yv);
	// user/subject
  yv = yajl_tree_get(yajl_node, user_path_1, yajl_t_string);
  if (yv)
    user_.subject_ = YAJL_GET_STRING(yv);
	// action/name 
  yv = yajl_tree_get(yajl_node, user_path_2, yajl_t_string);
  if (yv)
    user_.authkey_ = YAJL_GET_STRING(yv);
  // user namespace.name
  yv = yajl_tree_get(yajl_node, user_path_3, yajl_t_string);
  if (yv)
    user_.namespace_.name_ = YAJL_GET_STRING(yv);
  // user namespace.uuid
  yv = yajl_tree_get(yajl_node, user_path_4, yajl_t_string);
  if (yv)
    user_.namespace_.uuid_ = YAJL_GET_STRING(yv);
  // Clean up and return
  yajl_tree_free(yajl_node);
}
