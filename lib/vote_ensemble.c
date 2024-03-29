/* Copyright (C) 2019 John Törnblom

   This file is part of VoTE (Verifier of Tree Ensembles).

VoTE is free software: you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version.

VoTE is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
for more details.

You should have received a copy of the GNU Lesser General Public
License along with VoTE; see the files COPYING and COPYING.LESSER. If not,
see <http://www.gnu.org/licenses/>.  */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "parson.h"

#include "vote.h"
#include "vote_math.h"
#include "vote_tree.h"
#include "vote_pipeline.h"
#include "vote_refinary.h"
#include "vote_abstract.h"
#include "vote_postproc.h"


static struct json_value_t*
vote_ensemble_encode(const vote_ensemble_t* e) {
  struct json_value_t* root = json_value_init_object();
  struct json_value_t* val = json_value_init_array();
  struct json_array_t* array = json_value_get_array(val);
  struct json_object_t* obj = json_value_get_object(root);

  json_object_set_value(obj, "trees", val);
  for(size_t i=0; i<e->nb_trees; i++) {
    val = vote_tree_encode(e->trees[i]);
    json_array_append_value(array, val);
  }

  switch(e->post_process) {
  case VOTE_POST_PROCESS_NONE:
    json_object_set_string(obj, "post_process", "none");
    break;

  case VOTE_POST_PROCESS_DIVISOR:
    json_object_set_string(obj, "post_process", "divisor");
    break;

  case VOTE_POST_PROCESS_SOFTMAX:
    json_object_set_string(obj, "post_process", "softmax");
    break;

  case VOTE_POST_PROCESS_SIGMOID:
    json_object_set_string(obj, "post_process", "sigmoid");
    break;
  }

  return root;
}


static vote_ensemble_t*
vote_ensemble_load(struct json_value_t *root) {
  struct json_object_t *obj;
  struct json_array_t *array;

  assert(json_value_get_type(root) == JSONObject);
  obj = json_value_get_object(root);
  array = json_object_get_array(obj, "trees");
  assert(array);
  
  vote_ensemble_t *e = calloc(1, sizeof(vote_ensemble_t));
  assert(e);
  
  e->nb_trees = json_array_get_count(array);
  e->trees = calloc(e->nb_trees, sizeof(vote_tree_t*));
  assert(e->trees);
  
  for(size_t i=0; i<e->nb_trees; i++) {
    struct json_value_t *val = json_array_get_value(array, i);
    e->trees[i] = vote_tree_parse(val);

    if(i == 0) {
      e->nb_inputs = e->trees[i]->nb_inputs;
      e->nb_outputs = e->trees[i]->nb_outputs;
    } else {
      assert(e->nb_inputs == e->trees[i]->nb_inputs);
      assert(e->nb_outputs == e->trees[i]->nb_outputs);
    }
    e->nb_nodes += e->trees[i]->nb_nodes;
  }

  const char *post_process = json_object_get_string(obj, "post_process");
  assert(post_process);
  
  if(!strcmp(post_process, "none")) {
    e->post_process = VOTE_POST_PROCESS_NONE;
  } else if(!strcmp(post_process, "divisor")) {
    e->post_process = VOTE_POST_PROCESS_DIVISOR;
  } else if(!strcmp(post_process, "softmax")) {
    e->post_process = VOTE_POST_PROCESS_SOFTMAX;
  } else if(!strcmp(post_process, "sigmoid")) {
    e->post_process = VOTE_POST_PROCESS_SIGMOID;
  } else {
    assert(false && "unknown post-processing algorithm");
  }
  
  return e;
}


vote_ensemble_t*
vote_ensemble_load_file(const char *filename) {
  struct json_value_t *root = json_parse_file(filename);
  assert(root);

  vote_ensemble_t* e = vote_ensemble_load(root);
  json_value_free(root);
  
  return e;
}


bool
vote_ensemble_save_file(const vote_ensemble_t *e, const char *filename) {
  struct json_value_t *root = vote_ensemble_encode(e);
  return json_serialize_to_file(root, filename) == JSONSuccess;
}


vote_ensemble_t*
vote_ensemble_load_string(const char *string) {
  struct json_value_t *root = json_parse_string(string);
  assert(root);

  vote_ensemble_t* e = vote_ensemble_load(root);
  json_value_free(root);
  
  return e;
}


const char*
vote_ensemble_save_string(const vote_ensemble_t *e) {
  struct json_value_t *root = vote_ensemble_encode(e);
  return json_serialize_to_string(root);
}


void
vote_ensemble_del(vote_ensemble_t *e) {
  for(size_t i=0; i<e->nb_trees; i++) {
    vote_tree_del(e->trees[i]);
  }

  free(e->trees);
  free(e);
}


bool
vote_ensemble_forall(const vote_ensemble_t *e, const vote_bound_t *inputs,
		     vote_mapping_cb_t *user_cb, void *user_ctx) {
  vote_pipeline_t *head = vote_postproc_pipeline(e, user_ctx, user_cb);
    
  for(size_t i=0; i<e->nb_trees; i++) {
    vote_pipeline_t *sink = head;
    head = vote_refinary_pipeline(e->trees[e->nb_trees-i-1]);
    vote_pipeline_connect(head, sink);
  }    

  vote_mapping_t *m = vote_mapping_new(e->nb_inputs, e->nb_outputs);
  memcpy(m->inputs, inputs, e->nb_inputs * sizeof(vote_bound_t));
  vote_outcome_t o = vote_pipeline_input(head, m);

  vote_mapping_del(m);
  vote_pipeline_del(head);
  
  return o == VOTE_PASS;
}


bool
vote_ensemble_absref(const vote_ensemble_t *e, const vote_bound_t *inputs,
		     vote_mapping_cb_t *user_cb, void *user_ctx) {
  vote_pipeline_t *pp = vote_postproc_pipeline(e, user_ctx, user_cb);
  vote_pipeline_t *head = NULL;
  vote_pipeline_t *tail = NULL;
  
  for(size_t i=0; i<e->nb_trees; i++) {
    vote_pipeline_t *abs = vote_abstract_pipeline(&e->trees[i], e->nb_trees - i, pp);
    vote_pipeline_t *ref = vote_refinary_pipeline(e->trees[i]);
    vote_pipeline_connect(abs, ref);
    
    if(tail) {
      vote_pipeline_connect(tail, abs);
    }
    if(!head) {
      head = abs;
    }
    
    tail = ref;
  }    

  vote_pipeline_connect(tail, pp);
  
  vote_mapping_t *m = vote_mapping_new(e->nb_inputs, e->nb_outputs);
  memcpy(m->inputs, inputs, e->nb_inputs * sizeof(vote_bound_t));
  vote_outcome_t o = vote_pipeline_input(head, m);

  vote_mapping_del(m);
  vote_pipeline_del(head);
  
  return o == VOTE_PASS;
}


/**
 * Copy the output from a precise mapping to a vector of scalars.
 **/
static vote_outcome_t
vote_ensemble_copy_scalar_outputs(void *ctx, vote_mapping_t *m) {
  real_t *outputs = ctx;

  assert(vote_mapping_precise(m));
  
  for(size_t i=0; i<m->nb_outputs; i++) {
    outputs[i] = m->outputs[i].lower;
  }

  return VOTE_PASS;
}


void
vote_ensemble_eval(const vote_ensemble_t *e, const real_t *inputs, real_t *outputs) {
  vote_bound_t input_region[e->nb_inputs];

  for(size_t i=0; i<e->nb_inputs; i++) {
    input_region[i].lower = inputs[i];
    input_region[i].upper = inputs[i];
  }
  
  for(size_t i=0; i<e->nb_outputs; i++) {
    outputs[i] = VOTE_NAN;
  }
  
  vote_ensemble_forall(e, input_region, vote_ensemble_copy_scalar_outputs, outputs);
}


/**
 * Copy the output from one abstract mapping to another.
 **/
static vote_outcome_t
vote_ensemble_copy_mapping_outputs(void *ctx, vote_mapping_t *source) {
  vote_mapping_t *target = (vote_mapping_t*)ctx;

  memcpy(target->outputs, source->outputs,
	 source->nb_outputs * sizeof(vote_bound_t));
  
  return VOTE_PASS;
}


vote_mapping_t *
vote_ensemble_approximate(const vote_ensemble_t *e, const vote_bound_t *inputs) {
  vote_mapping_t *m = vote_mapping_new(e->nb_inputs, e->nb_outputs);
  vote_pipeline_t *pp = vote_postproc_pipeline(e, m, vote_ensemble_copy_mapping_outputs);
  vote_pipeline_t *a = vote_abstract_pipeline(e->trees, e->nb_trees, pp);

  vote_pipeline_connect(a, pp);
  memcpy(m->inputs, inputs, e->nb_inputs * sizeof(vote_bound_t));
  vote_pipeline_input(a, m);
  
  vote_pipeline_del(a);
  
  return m;
}
