#!/usr/bin/env python
# encoding: utf-8
# Copyright (C) 2018 John Törnblom
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING. If not see
# <http://www.gnu.org/licenses/>.
'''
Train a random forest classifier using the sklearn toolkit. Samples shall be
provided in the CSV file format using the comma delimiter (,) and with labels as
the last column. Trained models are serialized into a json format before being
persisted to disk.
'''

import collections
import json
import logging
import optparse
import sys

import numpy as np

from sklearn.ensemble import RandomForestClassifier


logger = logging.getLogger('train-sklearn')


def normalize(matrix):
    for row in matrix:
        row /= np.sum(row) or 1

    return matrix


def main():
    parser = optparse.OptionParser(usage='%prog [options] <csv filename>')
    parser.set_description(__doc__.strip())
    
    parser.add_option('-B', dest='nb_trees', action='store',
                      help='Train a forest with INTEGER number of trees',
                      metavar='INTEGER', default=1, type=int)
    
    parser.add_option('-d', dest='max_depth', action='store',
                      help='Limit depth of trees to INTEGER',
                      metavar='INTEGER', default=None, type=int)

    parser.add_option('-o', dest='output', action='store',
                      help='Save trained forest to PATH',
                      metavar='PATH', default=None)

    parser.add_option('-v', '--verbosity', dest='verbosity', action='count',
                      default=1, help='increase debug logging level')
    
    (opts, args) = parser.parse_args()
    if len(args) == 0 or not opts.output:
        parser.print_help()
        sys.exit(1)
        
    levels = {
              0: logging.ERROR,
              1: logging.WARNING,
              2: logging.INFO,
              3: logging.DEBUG,
    }
    logging.basicConfig(level=levels.get(opts.verbosity, logging.DEBUG))

    forest = RandomForestClassifier(max_depth=opts.max_depth,
                                    n_estimators=opts.nb_trees,
                                    n_jobs=4,
                                    random_state=12345)

    data = np.loadtxt(args[0], delimiter=',')
    features = data[:, :-1]
    labels = data[:,-1].astype('int')

    forest.fit(features, labels)
    
    logger.info('filename: %s', opts.output)
    logger.info('nb_inputs: %d', forest.n_features_)
    logger.info('nb_outputs: %d', forest.n_outputs_ * forest.n_classes_)
    logger.info('train set: %d samples', len(features))

    tree_obj_list = list()
    for tree in forest.estimators_:
        tree_obj = dict()
        tree_obj['nb_inputs'] = tree.n_features_
        tree_obj['nb_outputs'] = tree.n_classes_ * tree.n_outputs_
        tree_obj['left'] = tree.tree_.children_left.tolist()
        tree_obj['right'] = tree.tree_.children_right.tolist()
        tree_obj['feature'] = tree.tree_.feature.tolist()
        tree_obj['threshold'] = tree.tree_.threshold.tolist()
        tree_obj['value'] = normalize(np.squeeze(tree.tree_.value)).tolist()
        tree_obj_list.append(tree_obj)

    root_obj = dict(trees=tree_obj_list,
                    post_process='divisor')

    
    with open(opts.output, 'w') as f:
        json.dump(root_obj, f)
        

if __name__ == '__main__':
    main()
        
