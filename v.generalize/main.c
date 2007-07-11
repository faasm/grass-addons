
/****************************************************************
 *
 * MODULE:     v.generalize
 *
 * AUTHOR(S):  Daniel Bundala
 *
 * PURPOSE:    Module for line simplification and smoothing
 *
 * COPYRIGHT:  (C) 2002-2005 by the GRASS Development Team
 *
 *             This program is free software under the
 *             GNU General Public License (>=v2).
 *             Read the file COPYING that comes with GRASS
 *             for details.
 *
 ****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <grass/gis.h>
#include <grass/Vect.h>
#include <grass/glocale.h>
#include "misc.h"

#define DOUGLAS 0
#define LANG 1
#define VERTEX_REDUCTION 2
#define REUMANN 3
#define BOYLE 4
#define DISTANCE_WEIGHTING 5
#define CHAIKEN 6
#define HERMITE 7
#define SNAKES 8
#define DOUGLAS_REDUCTION 9
#define SLIDING_AVERAGING 10

int main(int argc, char *argv[])
{
    struct Map_info In, Out;
    static struct line_pnts *Points;
    struct line_cats *Cats;
    int i, type, cat, iter;
    char *mapset;
    struct GModule *module;	/* GRASS module for parsing arguments */
    struct Option *map_in, *map_out, *thresh_opt, *method_opt, *look_ahead_opt;
    struct Option *iterations_opt, *cat_opt, *alfa_opt, *beta_opt, *type_opt;
    struct Option *field_opt, *where_opt, *reduction_opt, *slide_opt;
    struct Option *angle_thresh_opt;
    struct Flag *ca_flag;
    int with_z;
    int total_input, total_output;	/* Number of points in the input/output map respectively */
    double thresh, alfa, beta, reduction, slide, angle_thresh;
    int method;
    int look_ahead, iterations;
    int chcat;
    int ret, layer;
    int n_areas;
    double x, y;
    int simplification, mask_type;
    VARRAY *varray;
    char *s;

    /* initialize GIS environment */
    G_gisinit(argv[0]);		/* reads grass env, stores program name to G_program_name() */

    /* initialize module */
    module = G_define_module();
    module->keywords = _("vector, generalization, simplification, smoothing");
    module->description = _("Line simplification and smoothing");

    /* Define the different options as defined in gis.h */
    map_in = G_define_standard_option(G_OPT_V_INPUT);
    map_out = G_define_standard_option(G_OPT_V_OUTPUT);

    type_opt = G_define_standard_option(G_OPT_V_TYPE);
    type_opt->options = "line,boundary";
    type_opt->answer = "line,boundary";

    method_opt = G_define_option();
    method_opt->key = "method";
    method_opt->type = TYPE_STRING;
    method_opt->required = YES;
    method_opt->multiple = NO;
    method_opt->options =
	"douglas,douglas_reduction,lang,reduction,reumann,boyle,sliding_averaging,distance_weighting,chaiken,hermite,snakes";
    method_opt->answer = "douglas";
    method_opt->descriptions = _("douglas;Douglas-Peucker Algorithm;"
				 "douglas_reduction;Douglas-Peucker Algorithm with reduction parameter;"
				 "lang;Lang Simplification Algorithm;"
				 "reduction;Vertex Reduction Algorithm eliminates points close to each other;"
				 "reumann;Reumann-Witkam Algorithm;"
				 "boyle;Boyle's Forward-Looking Algorithm;"
				 "sliding_averaging;McMaster's Sliding Averaging Algorithm;"
				 "distance_weighting;McMaster's Distance-Weighting Algorithm;"
				 "chaiken;Chaiken's Algorithm;"
				 "hermite;Interpolation by Cubic Hermite Splines;"
				 "snakes;Snakes method for line smoothing;");
    method_opt->description = _("Line simplification/smoothing algorithm");

    thresh_opt = G_define_option();
    thresh_opt->key = "threshold";
    thresh_opt->type = TYPE_DOUBLE;
    thresh_opt->required = YES;
    thresh_opt->options = "0-1000000000";
    thresh_opt->answer = "1.0";
    thresh_opt->description = _("Maximal tolerance value");

    look_ahead_opt = G_define_option();
    look_ahead_opt->key = "look_ahead";
    look_ahead_opt->type = TYPE_INTEGER;
    look_ahead_opt->required = YES;
    look_ahead_opt->answer = "7";
    look_ahead_opt->description = _("Look-ahead parameter");

    reduction_opt = G_define_option();
    reduction_opt->key = "reduction";
    reduction_opt->type = TYPE_DOUBLE;
    reduction_opt->required = YES;
    reduction_opt->answer = "50";
    reduction_opt->options = "0-100";
    reduction_opt->description =
	_
	("Percentage of the points in the output of 'douglas_reduction' algorithm");

    slide_opt = G_define_option();
    slide_opt->key = "slide";
    slide_opt->type = TYPE_DOUBLE;
    slide_opt->required = YES;
    slide_opt->answer = "0.5";
    slide_opt->options = "0-1";
    slide_opt->description =
	_("Slide of computed point toward the original point");

    angle_thresh_opt = G_define_option();
    angle_thresh_opt->key = "angle_thresh";
    angle_thresh_opt->type = TYPE_DOUBLE;
    angle_thresh_opt->required = YES;
    angle_thresh_opt->answer = "3";
    angle_thresh_opt->options = "0-180";
    angle_thresh_opt->description =
	_("Minimum angle between two consecutive segments in Hermite method");

    alfa_opt = G_define_option();
    alfa_opt->key = "alfa";
    alfa_opt->type = TYPE_DOUBLE;
    alfa_opt->required = YES;
    alfa_opt->answer = "1.0";
    alfa_opt->description = _("Snakes alfa parameter");

    beta_opt = G_define_option();
    beta_opt->key = "beta";
    beta_opt->type = TYPE_DOUBLE;
    beta_opt->required = YES;
    beta_opt->answer = "1.0";
    beta_opt->description = _("Snakes beta parameter");

    iterations_opt = G_define_option();
    iterations_opt->key = "iterations";
    iterations_opt->type = TYPE_INTEGER;
    iterations_opt->required = YES;
    iterations_opt->answer = "1";
    iterations_opt->description = _("Number of iterations");

    field_opt = G_define_standard_option(G_OPT_V_FIELD);
    cat_opt = G_define_standard_option(G_OPT_V_CATS);
    where_opt = G_define_standard_option(G_OPT_WHERE);


    ca_flag = G_define_flag();
    ca_flag->key = 'c';
    ca_flag->description = _("Copy attributes");

    /* options and flags parser */
    if (G_parser(argc, argv))
	exit(EXIT_FAILURE);

    thresh = atof(thresh_opt->answer);
    look_ahead = atoi(look_ahead_opt->answer);
    alfa = atof(alfa_opt->answer);
    beta = atof(beta_opt->answer);
    reduction = atof(reduction_opt->answer);
    iterations = atoi(iterations_opt->answer);
    slide = atof(slide_opt->answer);
    angle_thresh = atof(angle_thresh_opt->answer);

    mask_type = type_mask(type_opt);
    G_debug(3, "Method: %s", method_opt->answer);

    s = method_opt->answer;

    if (strcmp(s, "douglas") == 0)
	method = DOUGLAS;
    else if (strcmp(s, "lang") == 0)
	method = LANG;
    else if (strcmp(s, "reduction") == 0)
	method = VERTEX_REDUCTION;
    else if (strcmp(s, "reumann") == 0)
	method = REUMANN;
    else if (strcmp(s, "boyle") == 0)
	method = BOYLE;
    else if (strcmp(s, "distance_weighting") == 0)
	method = DISTANCE_WEIGHTING;
    else if (strcmp(s, "chaiken") == 0)
	method = CHAIKEN;
    else if (strcmp(s, "hermite") == 0)
	method = HERMITE;
    else if (strcmp(s, "snakes") == 0)
	method = SNAKES;
    else if (strcmp(s, "douglas_reduction") == 0)
	method = DOUGLAS_REDUCTION;
    else if (strcmp(s, "sliding_averaging") == 0)
	method = SLIDING_AVERAGING;
    else {
	G_fatal_error(_("Unknown method"));
	exit(EXIT_FAILURE);
    };


    /* simplification or smoothing? */
    switch (method) {
    case DOUGLAS:
    case LANG:
    case VERTEX_REDUCTION:
    case REUMANN:
	simplification = 1;
	break;
    default:
	simplification = 0;
	break;
    };


    Points = Vect_new_line_struct();
    Cats = Vect_new_cats_struct();

    Vect_check_input_output_name(map_in->answer, map_out->answer,
				 GV_FATAL_EXIT);

    if ((mapset = G_find_vector2(map_in->answer, "")) == NULL)
	G_fatal_error(_("Could not find input %s"), map_in->answer);

    Vect_set_open_level(2);

    if (1 > Vect_open_old(&In, map_in->answer, mapset))
	G_fatal_error(_("Could not open input"));

    with_z = Vect_is_3d(&In);

    if (0 > Vect_open_new(&Out, map_out->answer, with_z)) {
	Vect_close(&In);
	G_fatal_error(_("Could not open output"));
    }


    /* parse filter option and select appropriate lines */
    layer = atoi(field_opt->answer);
    if (where_opt->answer) {
	if (layer < 1)
	    G_fatal_error(_("'layer' must be > 0 for 'where'"));
	if (cat_opt->answer)
	    G_warning(_
		      ("'where' and 'cats' parameters were supplied, cat will be ignored"));
	chcat = 1;
	varray = Vect_new_varray(Vect_get_num_lines(&In));
	if (Vect_set_varray_from_db
	    (&In, layer, where_opt->answer, mask_type, 1, varray) == -1) {
	    G_warning(_("Cannot load data from a database"));
	};
    }
    else if (cat_opt->answer) {
	if (layer < 1)
	    G_fatal_error(_("'layer must be > 0 for 'cats'"));
	varray = Vect_new_varray(Vect_get_num_lines(&In));
	chcat = 1;
	if (Vect_set_varray_from_cat_string
	    (&In, layer, cat_opt->answer, mask_type, 1, varray) == -1) {
	    G_warning(_("Problem loading category values"));
	};
    }
    else
	chcat = 0;


    Vect_copy_head_data(&In, &Out);
    Vect_hist_copy(&In, &Out);
    Vect_hist_command(&Out);

    total_input = total_output = 0;
    i = 0;
    while ((type = Vect_read_next_line(&In, Points, Cats)) > 0) {
	i++;
	if (type == GV_CENTROID)
	    continue;		/* skip old centroids,
				 * we calculate new */
	total_input += Points->n_points;

	if ((type & mask_type) && (!chcat || varray->c[i])) {
	    int after = 0;
	    for (iter = 0; iter < iterations; iter++) {
		switch (method) {
		case DOUGLAS:
		    douglas_peucker(Points, thresh, with_z);
		    break;
		case DOUGLAS_REDUCTION:
		    douglas_peucker_reduction(Points, thresh, reduction,
					      with_z);
		    break;
		case LANG:
		    lang(Points, thresh, look_ahead, with_z);
		    break;
		case VERTEX_REDUCTION:
		    vertex_reduction(Points, thresh, with_z);
		    break;
		case REUMANN:
		    reumann_witkam(Points, thresh, with_z);
		    break;
		case BOYLE:
		    boyle(Points, look_ahead, with_z);
		    break;
		case SLIDING_AVERAGING:
		    sliding_averaging(Points, slide, look_ahead, with_z);
		    break;
		case DISTANCE_WEIGHTING:
		    distance_weighting(Points, slide, look_ahead, with_z);
		    break;
		case CHAIKEN:
		    chaiken(Points, thresh, with_z);
		    break;
		case HERMITE:
		    hermite(Points, thresh, angle_thresh, with_z);
		    break;
		case SNAKES:
		    snakes(Points, alfa, beta, with_z);
		    break;
		};
	    };


	    /* remove "oversimplified" lines */
	    if (simplification && type == GV_LINE &&
		Vect_line_length(Points) < thresh)
		continue;

	    after = Points->n_points;
	    total_output += after;
	    Vect_write_line(&Out, type, Points, Cats);
	}
	else {
	    total_output += Points->n_points;
	    Vect_write_line(&Out, type, Points, Cats);
	};
    }


    /* calculate new centroids */
    Vect_build_partial(&Out, GV_BUILD_ATTACH_ISLES, NULL);
    n_areas = Vect_get_num_areas(&Out);
    for (i = 1; i <= n_areas; i++) {
	Vect_get_area_cats(&In, i, Cats);
	ret = Vect_get_point_in_area(&Out, i, &x, &y);
	if (ret < 0) {
	    G_warning(_("Cannot calculate area centroid"));
	    continue;
	};
	Vect_reset_line(Points);
	Vect_append_point(Points, x, y, 0.0);
	Vect_write_line(&Out, GV_CENTROID, Points, Cats);
    };

    /* finally copy tables */
    if (ca_flag->answer)
	Vect_copy_tables(&In, &Out, layer);

    Vect_build(&Out, stdout);

    G_message("Number of vertices was reduced from %d to %d[%d%%]", total_input,
	      total_output, (total_output * 100) / total_input);

    Vect_close(&In);
    Vect_close(&Out);

    exit(EXIT_SUCCESS);
}
