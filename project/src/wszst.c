
/***************************************************************************
 *                         _______ _______ _______                         *
 *                        |  ___  |____   |  ___  |                        *
 *                        | |   |_|    / /| |   |_|                        *
 *                        | |_____    / / | |_____                         *
 *                        |_____  |  / /  |_____  |                        *
 *                         _    | | / /    _    | |                        *
 *                        | |___| |/ /____| |___| |                        *
 *                        |_______|_______|_______|                        *
 *                                                                         *
 *                            Wiimms SZS Tools                             *
 *                          https://szs.wiimm.de/                          *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the SZS project.                                 *
 *   Visit https://szs.wiimm.de/ for project details and sources.          *
 *                                                                         *
 *   Copyright (c) 2011-2024 by Dirk Clemens <wiimm@wiimm.de>              *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   See file gpl-2.0.txt or http://www.gnu.org/licenses/gpl-2.0.txt       *
 *                                                                         *
 ***************************************************************************/

#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <zlib.h>

#include "dclib-utf8.h"
#include "lib-analyze.h"
#include "lib-szs.h"
#include "lib-nintendo.h"
#include "lib-quicklz.h"
#include "lib-brres.h"
#include "lib-xbmg.h"
#include "lib-kcl.h"
#include "lib-kmp.h"
#include "lib-ledis.h"
#include "lib-mdl.h"
#include "lib-pat.h"
#include "lib-breff.h"
#include "lib-image.h"
#include "lib-common.h"
#include "lib-rkg.h"
#include "lib-model-dae.h"
#include "lib-brres-inject.h"

static ccp opt_parent = 0;
#include "lib-bzip2.h"
#include "lib-pack.h"
#include "lib-rarc.h"
#include "lib-rkc.h"
#include "lib-staticr.h"
#include "db-dol.h"
#include "crypt.h"
#include "ui.h" // [[dclib]] wrapper
#include "ui-wszst.c"
#include "db-mkw.h"
#include "lib-object.h"
#include "lib-checksum.h"
#include "lib-bflyt.h"
#include "lib-wc24.h"
#include "lib-bms.h"
#include "lib-nitro.h"
#include "lib-bch.h"
#include "lib-bcres.h"
#include "lib-passthru.h"
#include "lib-brres-model.h"
#include "lib-nsbmd.h"
#include "lib-bfres.h"
#include "lib-model-dae.h"

#if HAVE_WIIMM_EXT
  #include "lib-vehicle.h"
  #include "wcommand.h"
#endif

static inline bool is_ext ( ccp src, ccp ext )
{
    ccp dot = strrchr(src,'.');
    return dot && !strcasecmp(dot,ext);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			definitions			///////////////
///////////////////////////////////////////////////////////////////////////////

#define TITLE WSZST_SHORT ": " WSZST_LONG " v" VERSION " r" REVISION \
	" " SYSTEM2 " - " AUTHOR " - " DATE

#define TOOLSET_TITLE TOOLSET_SHORT ": " TOOLSET_LONG " v" VERSION " r" REVISION \
	" " SYSTEM2 " - " AUTHOR " - " DATE

//
///////////////////////////////////////////////////////////////////////////////

static void help_exit ( bool xmode )
{
    SetupPager();
    fputs( TITLE "\n", stdout );

    if (xmode)
    {
	int cmd;
	for ( cmd = 0; cmd < CMD__N; cmd++ )
	    PrintHelpCmd(&InfoUI_wszst,stdout,0,cmd,0,0,URI_HOME);
    }
    else
	PrintHelpCmd(&InfoUI_wszst,stdout,0,0,"HELP",0,URI_HOME);

    ClosePager();
    ExitFixed(ERR_OK);
}

///////////////////////////////////////////////////////////////////////////////

static void list_compressions_exit()
{
    SetupColors();
    int fw = GetTermWidth(80,40) - 1;
    if ( fw > 120 )
	fw = 120;

    PrintColoredLines(stdout,colout,0,fw,0,0,
	"\n{setup|%s}\n\n{caption|Compression modes:}\n"
	"\n"
	"There are several compression methods and most of them"
	" support different compression levels."
	" Each methods and level can be address by a numerical mode"
	" and some by a name.\n"
	"\n"
	"In general, 3 compression formats are supported: YAZ0/YAZ1, BZ (bzip2) and LZ (LZMA).\n"
	"\n"
	"The following list shows all modes."
	" A mode can by selected by option {opt|--compr=param} or {opt|-C param}.\n"
	"|[3,9,23]\n"
	"{heading| numbers\tnames\tdescription}\n"
	"{heading|%.*s\n"
	"\t{hl| -1\tUNCOMPRESSED}\t|"
		"Don't compress.\n"
	"\t{hl|  0\tNOCHUNKS}\t|"
		"{cmd|YAZ} only: Use only byte copies.\n"
	"\n"
	"\t{hl|  1\tFAST}\t|"
		"Fastest available standard compression.\n"
	"\t{hl|1-8\t}\t|"
		"Standard levels between {par|FAST} and {par|BEST}."
		" Do not use compression level >6 for {cmd|LZMA} if the file is intended"
		" for Mario Kart Wii, as too much memory is required for decoding.\n" 
	"\t{hl|  9\tBEST}\t|"
		"Best compression level.\n"
	"\t{hl|\tDEFAULT}\t|"
		"Default compression: {par|6} for {cmd|LZMA} and {par|9} for all other methods.\n"
	"\n"
	"\t\t{hl|TRY2 - TRY5}\t|"
		"Because of many repeated data, the best {cmd|BZ} compression mode varies."
		" Therefor the levels {par|TRY2} to {par|TRY5} (or short {par|T2} to {par|T5})"
		" are defined to find the best compression mode with testing the first"
		" N levels of {par|9, 1, 8, 2, 5}. {par|TRY2} (levels 9 and 1)"
		" is used for normalizing."
		" For other than {cmd|BZ} compressions, {par|BEST} is used instead.\n"
	"\n"
	"\t{hl| 10\tULTRA}\t|"
		"A special time-consuming compression method for {cmd|YAZ} only."
		" It is dedicated to competitions with strict size limitations.\n"
	"\n"
	" {hl|100-150}\t\t|"
		"This {cmd|YAZ} compression uses a back tracking"
		" algorithm with recursion depth between {par|0 and 50} (last 2 digits)."
		" Depths 0 and 1 are similar to mode {par|9}."
		" Each new depth doubles the number of calculated paths."
		" Because of some optimizations, a depth of +5 results"
		" in 10 to 15 times and not in 32 (2^5) times like expected."
		" This algorithm needs 4 times as much memory as the uncompressed file."
		" So it is dedicated to small files only.\n"
 #if HAVE_WIIMM_EXT && 0
	"{heading|%.*s\n"
	" {bad|500-530}\t\t|"
		"This {bad|experimental} {cmd|YAZ} compression uses a brute force"
		" algorithm with recursion depth from {par|0 to 30} (last 2 digits)."
		" It is a very time-consuming compression method"
		" and creates smaller files than {par|ULTRA}.\n"
	" {bad|600-630}\t\t|"
		"This {bad|experimental} {cmd|YAZ} compression"
		" is an optimized version of {par|100-130}.\n"
 #endif
	"{heading|%.*s\n"
	"\n"
	,TITLE
	,3*fw,ThinLine300_3
 #if HAVE_WIIMM_EXT && 0
	,3*fw,ThinLine300_3
 #endif
	,3*fw,ThinLine300_3
	);

    ExitFixed(ERR_OK);
}

///////////////////////////////////////////////////////////////////////////////

static void print_version_section ( bool print_sect_header )
{
    cmd_version_section(print_sect_header,WSZST_SHORT,WSZST_LONG,long_count-1);
}

///////////////////////////////////////////////////////////////////////////////

static void version_exit()
{
    if ( brief_count > 1 )
	fputs( VERSION "\n", stdout );
    else if (brief_count)
	fputs( VERSION " r" REVISION " " SYSTEM2 "\n", stdout );
    else if (print_sections)
	print_version_section(true);
    else if (long_count)
	print_version_section(false);
    else
	fputs( TITLE "\n", stdout );

    ExitFixed(ERR_OK);
}

///////////////////////////////////////////////////////////////////////////////

static void print_title ( FILE * f )
{
    static bool done = false;
    if (!done)
    {
	done = true;
	if (print_sections)
	    print_version_section(true);
	else if ( verbose >= 1 && f == stdout )
	    fprintf(f,"\n%s\n\n",TITLE);
	else
	    fprintf(f,"*****  %s  *****\n",TITLE);
    }
}

///////////////////////////////////////////////////////////////////////////////

static const KeywordTab_t * current_command = 0;

static void hint_exit ( enumError stat )
{
    if ( current_command )
	fprintf(stderr,
	    "-> Type '%s help %s' (pipe it to a pager like 'less') for more help.\n\n",
	    ProgInfo.progname, CommandInfo[current_command->id].name1 );
    else
	fprintf(stderr,
	    "-> Type '%s -h' or '%s help' (pipe it to a pager like 'less') for more help.\n\n",
	    ProgInfo.progname, ProgInfo.progname );
    ExitFixed(stat);
}

///////////////////////////////////////////////////////////////////////////////

static void set_all ( bool force_cut )
{
    if ( all_count++ || force_cut )
	opt_cut = true;
    opt_decode = opt_encode_all = true;
    opt_mipmaps = 1;
    opt_recurse = INT_MAX;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			sizeof data			///////////////
///////////////////////////////////////////////////////////////////////////////

static const sizeof_info_t sizeof_info_szs_list[] =
{
    SIZEOF_INFO_TITLE("SZS basics")
	SIZEOF_INFO_ENTRY(compatible_info_t)
	SIZEOF_INFO_ENTRY(SubFile_t)
	SIZEOF_INFO_ENTRY(SubFileList_t)
	SIZEOF_INFO_ENTRY(SubDirList_t)
	SIZEOF_INFO_ENTRY(SubDir_t)
	SIZEOF_INFO_ENTRY(SubFileIterator_t)
	SIZEOF_INFO_ENTRY(DataContainer_t)
	SIZEOF_INFO_ENTRY(search_filter_t)
	SIZEOF_INFO_ENTRY(SubstString_t)
	SIZEOF_INFO_ENTRY(repair_ff_t)
	SIZEOF_INFO_ENTRY(slot_info_t)
	SIZEOF_INFO_ENTRY(FormatFieldItem_t)
	SIZEOF_INFO_ENTRY(FormatField_t)
	SIZEOF_INFO_ENTRY(tiny_param_t)
	SIZEOF_INFO_ENTRY(ParamList_t)
	SIZEOF_INFO_ENTRY(List_t)
	SIZEOF_INFO_ENTRY(MemItem_t)
	SIZEOF_INFO_ENTRY(SetupDef_t)
	SIZEOF_INFO_ENTRY(ListDef_t)
	SIZEOF_INFO_ENTRY(SetupParam_t)
	SIZEOF_INFO_ENTRY(config_t)
	SIZEOF_INFO_ENTRY(data_tab_t)
	SIZEOF_INFO_ENTRY(output_mode_t)

    SIZEOF_INFO_TITLE("SZS basics: numeric")
	SIZEOF_INFO_ENTRY(tri_metrics_t)
	SIZEOF_INFO_ENTRY(octahedron_t)
	SIZEOF_INFO_ENTRY(cuboid_t)
	SIZEOF_INFO_ENTRY(IndexList_t)
	SIZEOF_INFO_ENTRY(FuncParam_t)
	SIZEOF_INFO_ENTRY(FuncTable_t)
	SIZEOF_INFO_ENTRY(FuncInfo_t)
	SIZEOF_INFO_ENTRY(Var_t)
	SIZEOF_INFO_ENTRY(VarMap_t)
	SIZEOF_INFO_ENTRY(ScanLoop_t)
	SIZEOF_INFO_ENTRY(ScanMacro_t)
	SIZEOF_INFO_ENTRY(ScanFile_t)
	SIZEOF_INFO_ENTRY(ScanInfo_t)
	SIZEOF_INFO_ENTRY(ScanParam_t)
	SIZEOF_INFO_ENTRY(Line_t)

    SIZEOF_INFO_TITLE("Archives and packing")
	SIZEOF_INFO_ENTRY(BZIP2_t)
	SIZEOF_INFO_ENTRY(BZ2Manager_t)
	SIZEOF_INFO_ENTRY(BZ2Source_t)
	SIZEOF_INFO_ENTRY(CompressManager_t)
	SIZEOF_INFO_ENTRY(sha1_type_t)
	SIZEOF_INFO_ENTRY(sha1_db_t)
	SIZEOF_INFO_ENTRY(pack_header_t)
	SIZEOF_INFO_ENTRY(pack_metric_t)
	SIZEOF_INFO_ENTRY(check_cache_t)

    SIZEOF_INFO_TITLE("BREFF & BREFT")
	SIZEOF_INFO_ENTRY(breff_root_head_t)
	SIZEOF_INFO_ENTRY(breff_root_t)
	SIZEOF_INFO_ENTRY(breff_item_name_t)
	SIZEOF_INFO_ENTRY(breff_item_data_t)
	SIZEOF_INFO_ENTRY(breff_item_list_t)
	SIZEOF_INFO_ENTRY(breft_image_t)

    SIZEOF_INFO_TITLE("BRRES")
	SIZEOF_INFO_ENTRY(brres_info_t)
	SIZEOF_INFO_ENTRY(brsub_list_t)
	SIZEOF_INFO_ENTRY(brsub_info_t)
	SIZEOF_INFO_ENTRY(brsub_iterator_t)
	SIZEOF_INFO_ENTRY(brres_t)
	SIZEOF_INFO_ENTRY(tex_info_t)
	SIZEOF_INFO_ENTRY(name_ref_elem_t)
	SIZEOF_INFO_ENTRY(name_ref_var_t)
	SIZEOF_INFO_ENTRY(name_ref_t)

    SIZEOF_INFO_TITLE("Common files")
	SIZEOF_INFO_ENTRY(object_type_t)
	SIZEOF_INFO_ENTRY(object_mgr_t)
	SIZEOF_INFO_ENTRY(itemslot_bin_t)
	SIZEOF_INFO_ENTRY(itemslot_t)
	SIZEOF_INFO_ENTRY(minigame_kmg_head_t)
	SIZEOF_INFO_ENTRY(minigame_kmg_t)
	SIZEOF_INFO_ENTRY(minigame_t)
	SIZEOF_INFO_ENTRY(objflow_bin_t)
	SIZEOF_INFO_ENTRY(objflow_t)
	SIZEOF_INFO_ENTRY(geohit_bin_t)
	SIZEOF_INFO_ENTRY(geohit_t)

    SIZEOF_INFO_TITLE("CT-CODE")
	SIZEOF_INFO_ENTRY(ctcode_sect_info_t)
	SIZEOF_INFO_ENTRY(ctcode_header_t)
	SIZEOF_INFO_ENTRY(ctcode_cup1_data_t)
	SIZEOF_INFO_ENTRY(ctcode_cup1_head_t)
	SIZEOF_INFO_ENTRY(ctcode_crs1_data_t)
	SIZEOF_INFO_ENTRY(ctcode_crs1_head_t)
	SIZEOF_INFO_ENTRY(ctcode_cupref_t)
	SIZEOF_INFO_ENTRY(ctcode_arena_t)
	SIZEOF_INFO_ENTRY(ctcode_t)

    SIZEOF_INFO_TITLE("DOL")
	SIZEOF_INFO_ENTRY(DolSectionMap_t)
	SIZEOF_INFO_ENTRY(DolAddressMap_t)

    SIZEOF_INFO_TITLE("File DB")
	SIZEOF_INFO_ENTRY(DbFile_t)
	SIZEOF_INFO_ENTRY(DbFileSZS_t)
	SIZEOF_INFO_ENTRY(DbFileSHA1_t)
	SIZEOF_INFO_ENTRY(DbFileFILE_t)
	SIZEOF_INFO_ENTRY(DbFileGROUP_t)

    SIZEOF_INFO_TITLE("Images")
	SIZEOF_INFO_ENTRY(MipmapOptions_t)
	SIZEOF_INFO_ENTRY(ImageGeometry_t)
	SIZEOF_INFO_ENTRY(Image_t)
	SIZEOF_INFO_ENTRY(GenericImgParam_t)
	SIZEOF_INFO_ENTRY(cmpr_info_t)
	SIZEOF_INFO_ENTRY(tpl_header_t)
	SIZEOF_INFO_ENTRY(tpl_header_ex_t)
	SIZEOF_INFO_ENTRY(tpl_imgtab_t)
	SIZEOF_INFO_ENTRY(tpl_pal_header_t)
	SIZEOF_INFO_ENTRY(tpl_img_header_t)
	SIZEOF_INFO_ENTRY(tpl_raw_t)
	SIZEOF_INFO_ENTRY(tpl_signature_t)
	SIZEOF_INFO_ENTRY(bti_header_t)

    SIZEOF_INFO_TITLE("KCL")
	SIZEOF_INFO_ENTRY(kcl_attrib_name_t)
	SIZEOF_INFO_ENTRY(kcl_class_t)
	SIZEOF_INFO_ENTRY(kcl_type_t)
	SIZEOF_INFO_ENTRY(kcl_analyze_t)
	SIZEOF_INFO_ENTRY(kcl_head_t)
	SIZEOF_INFO_ENTRY(kcl_triangle_t)
	SIZEOF_INFO_ENTRY(kcl_tridata_flt_t)
	SIZEOF_INFO_ENTRY(kcl_tridata_dbl_t)
	SIZEOF_INFO_ENTRY(kcl_tridata0_t)
	SIZEOF_INFO_ENTRY(kcl_tridata1_t)
	SIZEOF_INFO_ENTRY(kcl_t)
	SIZEOF_INFO_ENTRY(kcl_tri_param_t)
	SIZEOF_INFO_ENTRY(kcl_cube_t)
 #if SUPPORT_KCL_CUBE
	SIZEOF_INFO_ENTRY(kcl_cube_list_t)
 #endif
	SIZEOF_INFO_ENTRY(kcl_tri_t)

    SIZEOF_INFO_TITLE("KMP basics")
	SIZEOF_INFO_ENTRY(kmp_file_gen_t)
	SIZEOF_INFO_ENTRY(kmp_file_mkw_t)
	SIZEOF_INFO_ENTRY(kmp_file_wim0_t)
	SIZEOF_INFO_ENTRY(kmp_head_info_t)
	SIZEOF_INFO_ENTRY(kmp_list_head_t)
	SIZEOF_INFO_ENTRY(kmp_ktpt_entry_t)
	SIZEOF_INFO_ENTRY(kmp_enpt_entry_t)
	SIZEOF_INFO_ENTRY(kmp_enph_entry_t)
	SIZEOF_INFO_ENTRY(kmp_ckpt_entry_t)
	SIZEOF_INFO_ENTRY(kmp_gobj_entry_t)
	SIZEOF_INFO_ENTRY(kmp_poti_group_t)
	SIZEOF_INFO_ENTRY(kmp_poti_point_t)
	SIZEOF_INFO_ENTRY(kmp_area_entry_t)
	SIZEOF_INFO_ENTRY(kmp_came_entry_t)
	SIZEOF_INFO_ENTRY(kmp_jgpt_entry_t)
	SIZEOF_INFO_ENTRY(kmp_stgi_entry_t)
	SIZEOF_INFO_ENTRY(kmp_wim0_t)
	SIZEOF_INFO_ENTRY(kmp_wim0_sect_t)
	SIZEOF_INFO_ENTRY(kmp_wim0_info_t)
	SIZEOF_INFO_ENTRY(kmp_section_t)
	SIZEOF_INFO_ENTRY(kmp_rtype_info_t)
	SIZEOF_INFO_ENTRY(kmp_gopt_t)
	SIZEOF_INFO_ENTRY(kmp_gopt2_t)
	SIZEOF_INFO_ENTRY(kmp_group_elem_t)
	SIZEOF_INFO_ENTRY(kmp_group_t)
	SIZEOF_INFO_ENTRY(kmp_group_list_t)
	SIZEOF_INFO_ENTRY(kmp_rtobj_t)
	SIZEOF_INFO_ENTRY(kmp_rtobj_list_t)
	SIZEOF_INFO_ENTRY(kmp_ph_t)
	SIZEOF_INFO_ENTRY(kmp_flag_t)
	SIZEOF_INFO_ENTRY(kmp_t)
	SIZEOF_INFO_ENTRY(Itembox_t)
	SIZEOF_INFO_ENTRY(DrawKCL_t)
	SIZEOF_INFO_ENTRY(pos_param_t)
	SIZEOF_INFO_ENTRY(pos_file_t)
	SIZEOF_INFO_ENTRY(kmp_obj_info_t)

    SIZEOF_INFO_TITLE("KMP analysis")
	SIZEOF_INFO_ENTRY(kmp_linfo_t)
	SIZEOF_INFO_ENTRY(kmp_finish_t)
	SIZEOF_INFO_ENTRY(kmp_usedpos_obj_t)
	SIZEOF_INFO_ENTRY(kmp_usedpos_t)
	SIZEOF_INFO_ENTRY(kmp_pflags_t)
	SIZEOF_INFO_ENTRY(gobj_cond_ref_t)
	SIZEOF_INFO_ENTRY(gobj_cond_mask_t)
	SIZEOF_INFO_ENTRY(cond_ref_info_t)
	SIZEOF_INFO_ENTRY(kmp_ana_defobj_t)
	SIZEOF_INFO_ENTRY(kmp_ana_ref_t)
	SIZEOF_INFO_ENTRY(gobj_condition_t)
	SIZEOF_INFO_ENTRY(gobj_condition_set_t)
	SIZEOF_INFO_ENTRY(kmp_gobj_info_t)
	SIZEOF_INFO_ENTRY(kmp_ana_gobj_t)
	SIZEOF_INFO_ENTRY(kmp_ana_pflag_res_t)
	SIZEOF_INFO_ENTRY(kmp_ana_pflag_t)

    SIZEOF_INFO_TITLE("LE-CODE basics")
	SIZEOF_INFO_ENTRY(lecode_debug_info_t)
	SIZEOF_INFO_ENTRY(lecode_debug_ex_t)
	SIZEOF_INFO_ENTRY(le_lpar_t)
	SIZEOF_INFO_ENTRY(le_binary_head_v3_t)
	SIZEOF_INFO_ENTRY(le_binary_head_v4_t)
	SIZEOF_INFO_ENTRY(le_binary_head_v5_34_t)
	SIZEOF_INFO_ENTRY(le_binary_head_v5_38_t)
	SIZEOF_INFO_ENTRY(le_binary_head_v5_44_t)
	SIZEOF_INFO_ENTRY(le_binary_head_v5_t)
	SIZEOF_INFO_ENTRY(le_binary_head_t)
	SIZEOF_INFO_ENTRY(le_binary_param_t)
	SIZEOF_INFO_ENTRY(le_binpar_v1_35_t)
	SIZEOF_INFO_ENTRY(le_binpar_v1_37_t)
	SIZEOF_INFO_ENTRY(le_binpar_v1_f8_t)
	SIZEOF_INFO_ENTRY(le_binpar_v1_1b8_t)
	SIZEOF_INFO_ENTRY(le_binpar_v1_1bc_t)
	SIZEOF_INFO_ENTRY(le_binpar_v1_260_t)
	SIZEOF_INFO_ENTRY(le_binpar_v1_264_t)
	SIZEOF_INFO_ENTRY(le_binpar_v1_t)
	SIZEOF_INFO_ENTRY(le_cup_par_t)
	SIZEOF_INFO_ENTRY(le_course_par_t)
	SIZEOF_INFO_ENTRY(le_analyze_t)
	SIZEOF_INFO_ENTRY(le_region_t)
	SIZEOF_INFO_ENTRY(le_cup_track_t)
	SIZEOF_INFO_ENTRY(le_property_t)
	SIZEOF_INFO_ENTRY(le_music_t)
	SIZEOF_INFO_ENTRY(le_flags8_t)
	SIZEOF_INFO_ENTRY(le_flags_t)
	SIZEOF_INFO_ENTRY(LecodeFlags_t)

    SIZEOF_INFO_TITLE("LE-CODE distribution")
	SIZEOF_INFO_ENTRY(le_group_t)
	SIZEOF_INFO_ENTRY(le_track_type_t)
	SIZEOF_INFO_ENTRY(le_track_status_t)
	SIZEOF_INFO_ENTRY(le_track_text_t)
	SIZEOF_INFO_ENTRY(le_options_t)
	SIZEOF_INFO_ENTRY(le_cup_ref_t)
	SIZEOF_INFO_ENTRY(le_cup_t)
	SIZEOF_INFO_ENTRY(le_strpar_t)
	SIZEOF_INFO_ENTRY(le_track_id_t)
	SIZEOF_INFO_ENTRY(le_track_t)
	SIZEOF_INFO_ENTRY(le_track_arch_t)
	SIZEOF_INFO_ENTRY(le_auto_setup_t)
	SIZEOF_INFO_ENTRY(le_type_settings_t)
	SIZEOF_INFO_ENTRY(le_settings_t)
	SIZEOF_INFO_ENTRY(le_group_info_t)
	SIZEOF_INFO_ENTRY(le_distrib_t)
	SIZEOF_INFO_ENTRY(DistributionInfo_t)

    SIZEOF_INFO_TITLE("LEX")
	SIZEOF_INFO_ENTRY(features_szs_t)
	SIZEOF_INFO_ENTRY(check_features_szs_t)
	SIZEOF_INFO_ENTRY(have_lex_info_t)
	SIZEOF_INFO_ENTRY(lex_header_t)
	SIZEOF_INFO_ENTRY(lex_element_t)
	SIZEOF_INFO_ENTRY(lex_dev1_t)
	SIZEOF_INFO_ENTRY(lex_set1_t)
	SIZEOF_INFO_ENTRY(lex_ctdn_t)
	SIZEOF_INFO_ENTRY(lex_hipt_rule_t)
	SIZEOF_INFO_ENTRY(lex_ritp_rule_t)
	// [[new-lex-sect]]
	SIZEOF_INFO_ENTRY(lex_test_t)
	SIZEOF_INFO_ENTRY(lex_item_t)
	SIZEOF_INFO_ENTRY(lex_t)
	SIZEOF_INFO_ENTRY(lex_info_t)

    SIZEOF_INFO_TITLE("LE-CODE file types")
	SIZEOF_INFO_ENTRY(lta_header_t)
	SIZEOF_INFO_ENTRY(lta_node_par_t)
	SIZEOF_INFO_ENTRY(lta_node_record_t)
	SIZEOF_INFO_ENTRY(lta_node_t)
	SIZEOF_INFO_ENTRY(lta_manager_t)
	SIZEOF_INFO_ENTRY(lfl_header_t)
	SIZEOF_INFO_ENTRY(lfl_node_t)

    SIZEOF_INFO_TITLE("MDL")
	SIZEOF_INFO_ENTRY(mdl_head_t)
	SIZEOF_INFO_ENTRY(mdl_t)
	SIZEOF_INFO_ENTRY(mdl_sect0_t)
	SIZEOF_INFO_ENTRY(mdl_sect1_t)
	SIZEOF_INFO_ENTRY(mdl_sect2_t)
	SIZEOF_INFO_ENTRY(mdl_sect8_t)
	SIZEOF_INFO_ENTRY(mdl_sect8_layer_t)
	SIZEOF_INFO_ENTRY(Slot42MaterialInfo_t)
	SIZEOF_INFO_ENTRY(Slot42MaterialStat_t)
	SIZEOF_INFO_ENTRY(mdl_sect10_t)
	SIZEOF_INFO_ENTRY(mdl_minimap_t)
	SIZEOF_INFO_ENTRY(StringIteratorMDL_t)

    SIZEOF_INFO_TITLE("MKW")
	SIZEOF_INFO_ENTRY(TrackInfo_t)
	SIZEOF_INFO_ENTRY(MusicInfo_t)

    SIZEOF_INFO_TITLE("Object DB")
	SIZEOF_INFO_ENTRY(ObjSettingFormat_t)
	SIZEOF_INFO_ENTRY(ObjectInfo_t)
	SIZEOF_INFO_ENTRY(ObjSpec_t)
	SIZEOF_INFO_ENTRY(UsedObject_t)
	SIZEOF_INFO_ENTRY(UsedFile_t)
	SIZEOF_INFO_ENTRY(UsedFileSZS_t)
	SIZEOF_INFO_ENTRY(UsedFileSHA1_t)
	SIZEOF_INFO_ENTRY(UsedFileFILE_t)
	SIZEOF_INFO_ENTRY(UsedFileGROUP_t)

    SIZEOF_INFO_TITLE("PAT")
	SIZEOF_INFO_ENTRY(pat_head_t)
	SIZEOF_INFO_ENTRY(pat_s0_belem_t)
	SIZEOF_INFO_ENTRY(pat_s0_bhead_t)
	SIZEOF_INFO_ENTRY(pat_s0_sref_t)
	SIZEOF_INFO_ENTRY(pat_s0_selem_t)
	SIZEOF_INFO_ENTRY(pat_s0_shead_t)
	SIZEOF_INFO_ENTRY(pat_analyse_t)
	SIZEOF_INFO_ENTRY(pat_element_t)
	SIZEOF_INFO_ENTRY(pat_t)

    SIZEOF_INFO_TITLE("RARC")
	SIZEOF_INFO_ENTRY(rarc_file_header_t)
	SIZEOF_INFO_ENTRY(rarc_header_t)
	SIZEOF_INFO_ENTRY(rarc_node_t)
	SIZEOF_INFO_ENTRY(rarc_entry_t)

    SIZEOF_INFO_TITLE("RKC")
	SIZEOF_INFO_ENTRY(rkco_t)
	SIZEOF_INFO_ENTRY(rkct_t)

    SIZEOF_INFO_TITLE("RKG")
	SIZEOF_INFO_ENTRY(rkg_head_t)
	SIZEOF_INFO_ENTRY(rkg_info_t)

    SIZEOF_INFO_TITLE("StaticR")
	SIZEOF_INFO_ENTRY(str_server_list_t)
	SIZEOF_INFO_ENTRY(str_server_patch_t)
	SIZEOF_INFO_ENTRY(staticr_t)
	SIZEOF_INFO_ENTRY(VersusPointsInfo_t)
	SIZEOF_INFO_ENTRY(rel_header_t)
	SIZEOF_INFO_ENTRY(rel_sect_info_t)
	SIZEOF_INFO_ENTRY(rel_imp_t)
	SIZEOF_INFO_ENTRY(rel_data_t)
	SIZEOF_INFO_ENTRY(rel_info_t)
	SIZEOF_INFO_ENTRY(wpf_t)
	SIZEOF_INFO_ENTRY(wpf_head_t)
	SIZEOF_INFO_ENTRY(addr_port_t)
	SIZEOF_INFO_ENTRY(addr_port_version_t)

    SIZEOF_INFO_TITLE("SZS")
	SIZEOF_INFO_ENTRY(yaz0_header_t)
	SIZEOF_INFO_ENTRY(u8_header_t)
	SIZEOF_INFO_ENTRY(u8_node_t)
	SIZEOF_INFO_ENTRY(string_pool_t)
	SIZEOF_INFO_ENTRY(szs_subfile_t)
	SIZEOF_INFO_ENTRY(szs_subfile_list_t)
	SIZEOF_INFO_ENTRY(szs_have_t)
	SIZEOF_INFO_ENTRY(szs_file_t)
	SIZEOF_INFO_ENTRY(szs_u8_info_t)
	SIZEOF_INFO_ENTRY(szs_norm_t)
	SIZEOF_INFO_ENTRY(scan_data_t)
	SIZEOF_INFO_ENTRY(add_missing_t)
//	SIZEOF_INFO_ENTRY(check_szs_t)
	SIZEOF_INFO_ENTRY(slot_ana_t)
	SIZEOF_INFO_ENTRY(iterator_param_t)
	SIZEOF_INFO_ENTRY(szs_iterator_t)
	SIZEOF_INFO_ENTRY(brres_header_t)
	SIZEOF_INFO_ENTRY(brres_entry_t)
	SIZEOF_INFO_ENTRY(brres_group_t)
	SIZEOF_INFO_ENTRY(brres_root_t)
	SIZEOF_INFO_ENTRY(brsub_header_t)
	SIZEOF_INFO_ENTRY(brsub_cut_t)
	SIZEOF_INFO_ENTRY(szs_extract_t)
	SIZEOF_INFO_ENTRY(raw_data_t)
	SIZEOF_INFO_ENTRY(wbz_header_t)
	SIZEOF_INFO_ENTRY(wlz_header_t)
	SIZEOF_INFO_ENTRY(ybz_header_t)
	SIZEOF_INFO_ENTRY(ylz_header_t)
	SIZEOF_INFO_ENTRY(analyze_param_t)
	SIZEOF_INFO_ENTRY(analyze_szs_t)
	SIZEOF_INFO_ENTRY(file_type_t)
	SIZEOF_INFO_ENTRY(mkw_prefix_t)
	SIZEOF_INFO_ENTRY(mkw_category_t)
	SIZEOF_INFO_ENTRY(mkw_category_list_t)
	SIZEOF_INFO_ENTRY(split_filename_t)
	SIZEOF_INFO_ENTRY(print_split_par_t)
	SIZEOF_INFO_ENTRY(sha1_reference_t)

    SIZEOF_INFO_TERM()
};

//-----------------------------------------------------------------------------

static const sizeof_info_t *sizeof_info_szs[] =
{
    sizeof_info_szs_list,
    sizeof_info_bmg,
    0
};

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command test			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_test_options()
{
    printf("\nOptions (compatibility: %s; format: hex=dec):\n",PrintOptCompatible());

    printf("  test:        %16x = %12d\n",testmode,testmode);
    printf("  verbose:     %16x = %12d\n",verbose,verbose);
    printf("  warn modes:  %16llx = \"%s\"\n",(u64)WARN_MODE,GetWarnMode());
    printf("  force:       %16x = %12d (kmp=%d)\n",force_count,force_count,force_kmp);
    printf("  sort:        %16x = %12d\n",opt_sort,opt_sort);
    printf("  width:       %16x = %12d\n",opt_width,opt_width);
    printf("  escape-char: %16x = %12d\n",escape_char,escape_char);
    printf("  align:       %16x = %12d\n",opt_align,opt_align);
    printf("  align-u8:    %16x = %12d\n",opt_align_u8,opt_align_u8);
    printf("  align-lta:   %16x = %12d\n",opt_align_lta,opt_align_lta);
    printf("  align-pack:  %16x = %12d\n",opt_align_pack,opt_align_pack);
    printf("  align-brres: %16x = %12d\n",opt_align_brres,opt_align_brres);
    printf("  align-breff: %16x = %12d\n",opt_align_breff,opt_align_breff);
    printf("  align-breft: %16x = %12d\n",opt_align_breft,opt_align_breft);
    printf("  tiny:        %16x = %12d\n",opt_tiny,opt_tiny);
    printf("  compression:          mode %2d, level %d ==> %d\n",opt_compr_mode,opt_compr,GetComprByFF(fform_compr,0));
    printf("  recurse:     %16x = %12d\n",opt_recurse,opt_recurse);
    printf("  cmpr-default:         valid=%d, RGB565: %04x %04x, RGB: %06x %06x\n",
		opt_cmpr_valid,
		be16(opt_cmpr_def), be16(opt_cmpr_def+2),
		RGB565_to_RGB(be16(opt_cmpr_def)),
		RGB565_to_RGB(be16(opt_cmpr_def+2)) );
    printf("  n-mipmaps:   %16x = %12d\n",opt_n_images-1,opt_n_images-1);
    printf("  max-mipmaps: %16x = %12d\n",opt_max_images-1,opt_max_images-1);
    printf("  mipmap-size: %16x = %12d\n",opt_min_mipmap_size,opt_min_mipmap_size);
    printf("  mipmaps:     %16x = %12d\n",opt_mipmaps,opt_mipmaps);
    printf("  fast-mipmaps:%16x = %12d\n",fast_resize_enabled,fast_resize_enabled);
    printf("  analyse-mode:%16x = %12d\n",opt_analyze_mode,opt_analyze_mode);
    printf("  set-flags:   %16x = %12d\n",set_flags,set_flags);
    printf("  kcl modes:   %16llx = \"%s\"\n",(u64)KCL_MODE,GetKclMode());

    printf("  kmp modes:   %16llx = \"%s\"\n",(u64)KMP_MODE,GetKmpMode());
    printf("  kmp tform:   %16x = \"%s\"\n",KMP_TFORM,GetKmpTform());
    if (speed_mod_active)
	printf("  kmp speed modifier:      %04x ~ %5.3f\n",
		speed_mod_val, speed_mod_factor );
    if (opt_n_laps)
	printf("  kmp:stgi laps:%15x = %12d\n",opt_n_laps,opt_n_laps);
    printf("  mdl modes:   %16x = \"%s\"\n",MDL_MODE,GetMdlMode());
    printf("  pat modes:   %16x = \"%s\"\n",PAT_MODE,GetPatMode());

    if (have_patch_count)
	printf("  patch count: %16x = %12d\n",have_patch_count,have_patch_count);
    printf("  patch files: %16x = \"%s\"\n",PATCH_FILE_MODE,GetFileClassInfo());
    if (opt_slot)
	printf("  slot:        %16x = \"%s\"\n",opt_slot,PrintSlotMode(opt_slot));

    DumpTransformationOpt();

    if (opt_tracks)
	DumpTrackList(0,0,0);
    if (opt_arenas)
	DumpArenaList(0,0,0);

    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_test()
{
    SetPatchFileModeReadonly();

 #if 1 || !defined(TEST) // test options

    return cmd_test_options();

 #elif 1

    u8 * p1 = MALLOC(10);
    u8 * p2 = CALLOC(10,20);
    u8 * p3 = MALLOC(3000);
    DUMP_TRACE_ALLOC(stderr);
    p1[-1] = 'a';
    FREE(p1);
    FREE(p2);
    FREE(p3);
    DUMP_TRACE_ALLOC(stderr);
    return ERR_OK;

 #elif 1

    ParamList_t *param;
    for ( param = first_param; param; param = param->next )
    {
	SetupParam_t sp;
	InitializeSetupParam(&sp);
	enumError err = ScanSetupParam(&sp,true,param->arg,0,0,false);
	printf("err=%d, ff=%s, pt-dir=%d : %s\n",
		err, GetNameFF(sp.fform_file,sp.fform_arch),
		sp.have_pt_dir, param->arg );
	ResetSetupParam(&sp);
    }
    return ERR_OK;

 #elif 1

    const u64 base = 0x0807060504030201ull;
    u64 val;
	char buf[16];

    const endian_func_t * endian = &be_func;
    for(;;)
    {
	putchar('\n');

	memset(buf,0,sizeof(buf));
	endian->wr16(buf,base);
	HexDump16(stdout,0,0x16,buf,sizeof(buf));
	val = endian->rd16(buf);
	ASSERT_MSG( val == (u16)base, "%llx %llx\n", val, base );

	memset(buf,0,sizeof(buf));
	endian->wr24(buf,base);
	HexDump16(stdout,0,0x24,buf,sizeof(buf));
	val = endian->rd24(buf);
	ASSERT_MSG( val == ( (u32)base & 0xffffff ), "%llx %llx\n", val, base );

	memset(buf,0,sizeof(buf));
	endian->wr32(buf,base);
	HexDump16(stdout,0,0x32,buf,sizeof(buf));
	val = endian->rd32(buf);
	ASSERT_MSG( val == (u32)base, "%llx %llx\n", val, base );

	memset(buf,0,sizeof(buf));
	endian->wr48(buf,base);
	HexDump16(stdout,0,0x48,buf,sizeof(buf));
	val = endian->rd48(buf);
	ASSERT_MSG( val == ( base & 0xffffffffffff ), "%llx %llx\n", val, base );

	memset(buf,0,sizeof(buf));
	endian->wr64(buf,base);
	HexDump16(stdout,0,0x64,buf,sizeof(buf));
	val = endian->rd64(buf);
	ASSERT_MSG( val == base, "%llx %llx\n", val, base );

	if ( endian == &le_func )
	    break;
	endian = &le_func;
    }
    putchar('\n');
    return ERR_OK;

 #elif 1

    ParamList_t *param;
    for ( param = first_param; param; param = param->next )
    {
	ccp arg = param->arg;
	char buf[1000];
	ccp subpath = SplitSubPath(buf,sizeof(buf),arg);
	if (!subpath)
	    printf("NOT FOUND: |%s|\n",buf);
	else if (!*subpath)
	    printf("REAL FILE: |%s|\n",buf);
	else
	    printf("SUB PATH:  |%s|%s|\n", buf, subpath );
    }
    return ERR_OK;

 #endif
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command ui-check		///////////////
///////////////////////////////////////////////////////////////////////////////

enumError cmd_ui_check()
{
    SetupPager();

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    const bool have_dump = long_count >= 1;
    const bool have_header = !have_dump && !brief_count && print_header;

    int sep_len = 0;
    if (have_header)
    {
	int fw_file = 4;
	ccp *ptr = plist.field, *end;
	for ( end = ptr + plist.used; ptr < end; ptr++ )
	{
	    const int fw = strlen8(*ptr);
	    if ( fw_file < fw )
		 fw_file = fw;
	}
	sep_len = ( fw_file + 23 ) * 3;
	printf("\n%s Type       L Korean  File\n%s%.*s%s\n",
		colset->heading, colset->heading, sep_len, ThinLine300_3, colset->reset );
    }

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadSZS(&szs,arg,false,opt_ignore>0,true);
	if ( err > ERR_WARNING || err == ERR_NOT_EXISTS )
	    continue;

	DecompressSZS(&szs,true,0);

	if (have_dump)
	{
	    u_usec_t dur = -GetTimerUSec();
	    ui_check_t uc;
	    UiCheck(&uc,&szs);
	    dur += GetTimerUSec();

	    printf("%c%c "
		,uc.is_ui       >= OFFON_ON ? 'U' : uc.is_ui	   > OFFON_OFF ? 'u' : '-'
		,uc.is_korean   >= OFFON_ON ? 'K' : uc.is_korean   > OFFON_OFF ? 'k' : '-'
		);

	    for ( ui_type_t t = 1; t < UIT__N; t++ )
	    {
		uint mask = 1 << t;
		putchar( uc.possible & mask ? ui_type_char[t] : '-');
	    }

	    printf(" %c%c %6lluµs  %s\n",ui_type_char[uc.ui_type],uc.ui_lang,dur,arg);
	}
	else
	{
	    ui_check_t uc;
	    UiCheck(&uc,&szs);

	    ccp col = GetUiTypeColor(colout,uc.type);
	    printf("%s %-10s %c %-6s  %s%s\n",
		col,
		ui_type_name[uc.ui_type], uc.ui_lang,
		uc.is_korean > OFFON_AUTO ? "Korean" : "-",
		arg, *col ? colout->reset : "" );
	}
    }

    if (have_header)
    {
	if ( plist.used >= 3 )
	    printf("%s%.*s%s\n\n", colset->heading, sep_len, ThinLine300_3, colset->reset );
	else
	    putchar('\n');
    }

    ResetStringField(&plist);
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command autoadd			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError exec_autoadd
(
    UsedFileFILE_t	*used,		// vector with used files
    ccp			source		// filename of source file
)
{
    DASSERT(used);
    DASSERT(source);
    PRINT("exec_autoadd() sizeof(used)==%zu\n",sizeof(*used));

    char path_buf[PATH_MAX];

    szs_file_t szs;
    InitializeSZS(&szs);
    enumError err = LoadSZS(&szs,source,true,opt_ignore>0,true);

    if ( verbose >= 0 || testmode )
    {
	fprintf(stdlog,"%sANALYZE %s:%s\n",
		    verbose > 0 ? "\n" : "",
		    GetNameFF_SZS(&szs), szs.fname );
	fflush(stdlog);
    }

    if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
    {
	CollectFilesSZS(&szs,true,0,-1,SORT_NONE);

	int idx;
	for ( idx = 0; idx < N_DB_FILE_FILE; idx++ )
	{
	    if (used->d[idx])
		continue;

	    const DbFileFILE_t *ptr = DbFileFILE + idx;
	    if (!DBF_ARCH_SUPPORT(ptr->flags))
		continue;

	    szs_subfile_t *file, *file_end = szs.subfile.list + szs.subfile.used;
	    for ( file = szs.subfile.list; file < file_end; file++ )
	    {
		ccp fpath = file->path;
		if( fpath[0] == '.' && fpath[1] == '/' )
		    fpath += 2;
		if (!strcmp(fpath,ptr->file))
		{
		    used->d[idx] = 2;
		    ccp path = PathCatPP(path_buf,sizeof(path_buf),opt_dest,ptr->file);
		    if ( verbose >= 0 || testmode )
			fprintf(stdlog,"  %sADD %7u, %s:%s\n",
			    testmode ? "WOULD " : "",
			    file->size, GetNameFF(0,ptr->fform), path );

		    File_t F;
		    CreateFileOpt(&F,true,path,testmode,0);
		    if (F.f)
		    {
			if (opt_preserve)
			    SetFileAttrib(&F.fatt,&szs.fatt,0);
			size_t wstat = fwrite( szs.data + file->offset,
						    1, file->size, F.f );
			if ( wstat != file->size )
			    err = FILEERROR1(&F,ERR_WRITE_FAILED,
				    "Writing %u bytes failed: %s\n",
				    file->size, path );
		    }
		    ResetFile(&F,opt_preserve);
		}
	    }
	}
	fflush(stdout);
    }

    ResetSZS(&szs);
    return err;
}

///////////////////////////////////////////////////////////////////////////////

static enumError find_autoadd
(
    UsedFileFILE_t	*used,		// vector with used files
    ccp			source_dir,	// filename of source directory
    ccp			name,		// name of while without extension
    ccp			slot		// NULL or slot prefix
)
{
    DASSERT(used);
    DASSERT(source_dir);
    DASSERT(name);

    struct stat st;
    char pathbuf[PATH_MAX], fname[50];

    int try;
    for ( try = 0;; try++ )
    {
	switch (try)
	{
	    case 0:
		snprintf(fname,sizeof(fname),
			"%s", name );
		break;

	    case 1:
		snprintf(fname,sizeof(fname),
			"%s_d", name );
		break;

	    case 2:
		if (!slot)
		    *fname = 0;
		else
		    snprintf(fname,sizeof(fname),
			"%s-%s", slot, name );

		break;

	    case 3:
		if (!slot)
		    *fname = 0;
		else
		    snprintf(fname,sizeof(fname),
			"%s-%s_d", slot, name );
		break;

	    default:
		return ERR_OK;
	}

	if (*fname)
	{
	    ccp path = PathCatPPE(pathbuf,sizeof(pathbuf), source_dir, fname, ".szs" );
	    PRINT("-> %s\n",path);
	    if ( !stat(path,&st) && S_ISREG(st.st_mode) )
		return exec_autoadd(used,path);
	}
    }
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_autoadd()
{
    SetPatchFileModeReadonly();

    char path_buf[PATH_MAX];
    const config_t *config = GetConfig();
    CheckOptDest(config->autoadd_path,true);

    if ( !opt_mkdir && !ExistDirectory(opt_dest,0) )
	return ERROR0(ERR_CANT_CREATE_DIR,
	    "Directory does not exist: %s\n",opt_dest);
    opt_mkdir = true;
    autoadd_destination = opt_dest;

    if (!n_param)
	opt_overwrite = false;

    if ( !n_param || verbose >= 0 )
	fprintf(stdlog,"\nCURRENT AUTO-ADD PATH: %s\n",opt_dest);

    if ( !n_param || verbose >= 1 )
    {
	const StringField_t *sf = GetAutoaddList();
	int i;
	ccp *str = sf->field;
	for ( i = 0; i < sf->used; i++, str++ )
	    fprintf(stdlog,"       SEARCH PATH[%d]: %s\n",i,*str);
    }


    //--- create file list

    UsedFileFILE_t used;
    memset(&used,0,sizeof(used));
    uint n_invalid = 0;

    if ( !opt_overwrite || !n_param )
    {
	if ( verbose >= 2 )
	    fprintf(stdlog,"\nLIST OF FILES IN %s\n", opt_dest );

	int count = 0;
	const DbFileFILE_t *ptr;
	for ( ptr = DbFileFILE; ptr->file; ptr++ )
	    if (DBF_ARCH_SUPPORT(ptr->flags))
	    {
		ccp path = PathCatPP(path_buf,sizeof(path_buf),opt_dest,ptr->file);
		struct stat st;
		if ( !stat(path,&st) && S_ISREG(st.st_mode) )
		{
		    used.d[ptr-DbFileFILE] = 1;
		    if ( verbose >= 2 )
		    {
			count++;
			fprintf(stdlog,"  %-5s %s\n",
			    GetNameFF(0,ptr->fform), ptr->file );
		    }

		    if ( verbose >= 1 )
		    {
			DASSERT( ptr->ref < N_DB_FILE_REF_FILE );
			const s16 ref = DbFileRefFILE[ptr->ref];
			noPRINT("%zd -> %d/%d : %d : %s\n",
				ptr-DbFileFILE, ptr->ref ,ptr->group, ref, ptr->file );
			if ( ref >= 0 )
			{
			    DASSERT( ref < N_DB_FILE );
			    const DbFile_t *db = DbFile + ref;
			    DASSERT( db->sha1 < N_DB_FILE_SHA1 );
			    const u8 *sha1 = DbFileSHA1[db->sha1].sha1;

			    sha1_size_hash_t ss;
			    GetSSByFile(&ss,path,0);
			    if (memcmp(sha1,ss.hash,sizeof(ss.hash)))
			    {
				n_invalid++;
				used.d[ptr-DbFileFILE] = 2;
				HexDump(stdout,0,0,4,20,sha1,20);
				HexDump(stdout,0,0,4,20,ss.hash,20);
			    }
			}
		    }
		}
	    }
	if (count)
	    fprintf(stdlog,"  => %u of %zu files found.\n",count,sizeof(used));
    }


    //--- iterate parameters

    enumError max_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];
	if (!ExistDirectory(arg,0))
	{
	    enumError err = exec_autoadd(&used,arg);
	    if ( max_err < err )
		max_err = err;
	}
	else
	{
	    const DbFileSZS_t *db;
	    for ( db = DbFileSZS; db->szs_name; db++ )
		if ( (db->type & (FLT_OLD|FLT_STD)) == FLT_STD )
		    find_autoadd( &used, arg, db->szs_name,
					db->idx < 0 ? 0 : db->id+1 );
	}
    }

    ResetStringField(&plist);


    //--- print missing list

    if ( verbose >= 1 )
    {
	fprintf(stdlog,"\nLIST OF MISSED FILES IN %s\n", opt_dest );

	int count = 0;
	const DbFileFILE_t *ptr;
	for ( ptr = DbFileFILE; ptr->file; ptr++ )
	    if ( !used.d[ptr-DbFileFILE] && DBF_ARCH_SUPPORT(ptr->flags) )
	    {
		count++;
		fprintf(stdlog,"  %-5s %s\n",
			GetNameFF(0,ptr->fform), ptr->file );
	    }
	if (count)
	    fprintf(stdlog,"  => %u of %zu files missed.\n",count,sizeof(used));
    }


    //--- print invalid list

    if (n_invalid)
    {
	fprintf(stdlog,"\nLIST OF INVALID FILES IN %s\n", opt_dest );

	const DbFileFILE_t *ptr;
	for ( ptr = DbFileFILE; ptr->file; ptr++ )
	    if ( used.d[ptr-DbFileFILE] == 2 )
		fprintf(stdlog,"  %-5s %s\n",
			GetNameFF(0,ptr->fform), ptr->file );
	fprintf(stdlog,"  => %u of %zu files invalid.\n",n_invalid,sizeof(used));
    }

    if ( verbose >= 1 )
	fputs("\n",stdlog);

    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command files			///////////////
///////////////////////////////////////////////////////////////////////////////

static int files_func
(
    ccp		fname,		// name of current file without '.szs' extension
    ccp		id,		// unique ID of current file
    FileList_t	mode,		// mode of current file
    const TrackInfo_t *ti,	// NULL or related track info
    void	*param		// user defined parameter
)
{
    char name[BMG_MSG_BUF_SIZE];
    name[0] = 0;
    const bmg_item_t *bi = 0;

    if ( ti && opt_load_bmg.item_used )
    {
	bi = FindItemBMG(&opt_load_bmg,ti->track_id+MID_LE_TRACK_BEG);
	if ( !bi || !bi->len )
	{
	    bi = FindItemBMG(&opt_load_bmg,ti->track_id+MID_CT_TRACK_BEG);
	    if ( !bi || !bi->len )
	    {
		bi = FindItemBMG(&opt_load_bmg,ti->bmg_mid);
		if ( bi && !bi->len )
		    bi = 0;
	    }
	}

	if (bi)
	{
	    PrintString16BMG(name,sizeof(name)-10,bi->text,bi->len,BMG_UTF8_MAX,0,0);
	    if (!strchr(name,'['))
	    {
		const uint pos = strlen(name);
		snprintf(name+pos,sizeof(name)-pos," [%c%02u]",
			mode & FLT_ARENA ? 'a' : 'r', ti->def_slot );
	    }
	}
    }

    if (pipe_count)
    {
	if (verbose>0)
	    printf("%s|0x%02x|%s|%s\n",id,mode,fname,name);
	else
	    printf("%s|%s|%s\n",id,fname,name);
    }
    else
    {
	if (verbose>0)
	    printf("%-5s 0x%02x  ",id,mode);
	else
	    printf("%-5s ",id);

	if (*name)
	    printf("%-21s %s\n",fname,name);
	else
	    printf("%s\n",fname);
    }
    return 1;
}

//-----------------------------------------------------------------------------

static enumError cmd_tracks()
{
    SetPatchFileModeReadonly();

    u32 mode = 0;
    if (first_param)
    {
	ScanNumU32(first_param->arg,0,&mode,0,FLT_M_ALL);
	mode &= FLT_M_ALL;
    }

    if (!mode)
    {
	mode = FLT_TRACK | FLT_ARENA | FLT_STD;

	if ( all_count > 1 )
	    mode = FLT_M_ALL;
	else if ( all_count )
	    mode |= FLT_D;

	if (long_count)
	{
	    mode |= FLT_OTHER;
	    if ( long_count > 1 )
		mode |= FLT_OLD;
	}
    }

    IterateTrackFiles(mode,files_func,0);
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command scancache		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_scancache()
{
    if (opt_cache)
	AddParam(opt_cache);

    if ( n_param != 1 )
	return ERROR0(ERR_SYNTAX,"Exact one parameter (directory) expected.\n");

    szs_cache_dir = first_param->arg;
    if (!IsDirectory(szs_cache_dir,false))
	return ERROR0(ERR_SEMANTIC,"Directory expected: %s\n",szs_cache_dir);

    bool force = false;
    parallel_count = 0;
    if (opt_fast)
    {
	LoadSZSCache();
	force = true;
    }
    else
	ScanSZSCache(first_param->arg,opt_purge);

    return SaveSZSCache(force);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command export			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_export()
{
    SetPatchFileModeReadonly();
    DefineDefaultParserFunc();
    return ExportHelper("");
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command sizeof			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_sizeof()
{
    sizeof_info_order_t order;
    switch (GetSortMode(opt_sort,0,SORT_NONE))
    {
     case SORT_INAME:	order = SIZEOF_ORDER_NAME; break;
     case SORT_SIZE:	order = SIZEOF_ORDER_SIZE; break;
     default:		order = SIZEOF_ORDER_NONE; break;
    }

    SetupPager();
    PrintMode_t pm = { .fout = stdout, .cout = colout, .debug = logging };
    ArgManager_t am = { .force_case = LOUP_LOWER };
    for ( ParamList_t *param = first_param; param; param = param->next )
	AppendArgManager(&am,param->arg,0,false);

    const sizeof_info_t **si_list;
    if (brief_count)
	si_list = sizeof_info_szs;
    else
    {
	AddListToSizeofInfoMgr(sizeof_info_szs);
	si_list = GetSizeofInfoMgrList();
    }

    ListSizeofInfo(&pm,si_list,&am,order);

    ClosePager();
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command _CODE			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_code()
{
    SetPatchFileModeReadonly();
    stdlog = stderr;

    if (!n_param)
	AddParam("-");

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	if (verbose)
	    fprintf(stderr,"CODE %s\n",arg);
	File_t F;
	if (!OpenFILE(&F,true,arg,false,true))
	{
	    while ( !feof(F.f) && !ferror(F.f) )
	    {
		uint size = fread(iobuf,1,sizeof(iobuf),F.f);
		u8 *src = (u8*)iobuf, *end = src + size;
		while ( src < end )
		    *src++ ^= 0xdc;
		fwrite(iobuf,1,size,stdout);
	    }
	}
	ResetFile(&F,false);
    }

    ResetStringField(&plist);
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command _RECODE			///////////////
///////////////////////////////////////////////////////////////////////////////

#ifndef HAVE_WIIMM_EXT

 enumError cmd_recode()
 {
    ERROR0(ERR_NOT_IMPLEMENTED,
	"Command _RECODE not implemented in this version!\n");
    ExitFixed(ERR_NOT_IMPLEMENTED);
 }

#endif

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command _SUBFILE		///////////////
///////////////////////////////////////////////////////////////////////////////

enumError cmd_subfile()
{
    if (!first_param)
	return ERR_OK;

    SubDir_t dir;
    InitializeSubDir(&dir);

    NORMALIZE_FILENAME_PARAM(first_param);

    szs_file_t szs;
    InitializeSZS(&szs);
    enumError err = LoadSZS(&szs,first_param->arg,false,opt_ignore>0,true);
    if (!err)
    {
	DecompressSZS(&szs,true,0);
	#ifdef TEST
	{
	    printf("SZS: %p %zu / %p %zu\n",szs.cdata,szs.csize,szs.data,szs.size);

	    DumpInfoContainer(stdlog,collog,2,"SZS:  ",&szs.container,0x20);
	    szs_file_t szs2;
	    InitializeSubSZS(&szs2,&szs,4,12,FF_UNKNOWN,0,false);
	    printf("SZS2: %p %zu / %p %zu\n",szs2.cdata,szs2.csize,szs2.data,szs2.size);
	    DumpInfoContainer(stdlog,collog,2,"SZS1: ",&szs.container,0x20);
	    DumpInfoContainer(stdlog,collog,2,"SZS2: ",&szs2.container,0x20);
	    ResetSZS(&szs2);
	    DumpInfoContainer(stdlog,collog,2,"SZS:  ",&szs.container,0x20);
	}
	#endif
	DASSERT( !szs.file_size || szs.file_size >= szs.size );
	have_patch_count -= 1000000;
	PRINT("EXTRACT/%s[%s]: %s\n",__FUNCTION__,GetNameFF_SZS(&szs),szs.fname);
	err = ExtractFilesSZS(&szs,0,false,&dir,0);
	have_patch_count += 1000000;

	if ( opt_dest && *opt_dest )
	{
	    DumpSubDir(stdout,2,&dir);
	    SaveSubFiles(&dir,opt_dest);
	}

	SubFile_t *sf = FindSubFile(&dir,MemByString("3DModels(NW4R)/course"));
	if (sf)
	{
	    printf("** %s found [%p,0x%x] **\n",sf->fname,sf->data,sf->size);
	 #if 1
	    //ccp err = RenameForSlot42MDL(sf->data,sf->size,ContainerSZS(&szs));
	    ccp err = RenameForSlot42MDL(sf->data,sf->size,0);
	    if (err)
		printf("\e[35;1m %s \e[0m\n",err);
	    printf(">> SAVE %p,0x%x\n",sf->data,sf->size);
	    SaveFile("_mdl.tmp",0,FM_OVERWRITE|FM_TOUCH,sf->data,sf->size,0);
	 #else
	    mdl_t mdl;
	    ScanMDL(&mdl,true,sf->data,sf->size,ContainerSZS(&szs),0);
	    SetupSlot42Materials();
	    AppendMDL(&mdl,&slot42_mdl);
	    SaveRawMDL(&mdl,"_mdl.tmp",false);
	    ResetMDL(&mdl);
	 #endif
	}

	szs_file_t szs2;
	InitializeSZS(&szs2);
	enumError err = CreateSZS(&szs2,0,0,&dir,0,0,
			verbose > 0 ? UINT_MAX : 0, false );
	if (!err)
	    SaveSZS(&szs2,"_szs.tmp",true,false);
	ResetSZS(&szs2);
    }
    ResetSubDir(&dir);
    ResetSZS(&szs);
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command _TESTNORM		///////////////
///////////////////////////////////////////////////////////////////////////////

void PrintNorm ( const szs_norm_t *norm )
{
    if ( !norm || !norm->f )
	return;
}

///////////////////////////////////////////////////////////////////////////////

void SetupNormByOptions ( szs_norm_t *norm )
{
    DASSERT(norm);
    memset(norm,0,sizeof(*norm));
    memset(norm->modified,'-',SZI__N);
}

///////////////////////////////////////////////////////////////////////////////

enumError cmd_testnorm()
{
    if (!first_param)
	return ERR_OK;

    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
///////////////			command BRSUB			///////////////
///////////////////////////////////////////////////////////////////////////////
// [[check_brsub_t]]

typedef struct check_brsub_t
{
    szs_file_t		* szs;		// pointer to valid SZS
    int			mode;		// 0:warnings, 1:+hints, 2:+info

    bool		header_printed;	// true: header with filename printed
    bool		print_sep;	// true: print separator
    int			sep_fw;		// field width of separator
    uint		sep_count;	// number of printed lines after last sep

    enumError		max_err;	// error code
    ccp			brres_path;	// path of current BRRES
    uint		line_count;	// number of printed status lines
}
check_brsub_t;

///////////////////////////////////////////////////////////////////////////////

static int check_brsub
(
    struct szs_iterator_t	*it,	// iterator struct with all infos
    bool			term	// true: termination hint
)
{
    DASSERT(it);
    check_brsub_t *cb = (check_brsub_t*)it->param;
    DASSERT(cb);

    if (term)
	cb->brres_path = 0;
    else if ( it->size >= 0x0c )
    {
	u8 *data = it->szs->data + it->off;
	const file_format_t fform = GetByMagicFF(data,it->size,it->size);
	if ( fform == FF_BRRES )
	{
	    cb->brres_path = it->path;
	    cb->print_sep = print_header;
	}
	else if ( cb->brres_path && IsBRSUB(fform) )
	{
	    char stat_buf[50], vers_buf[50], ns_buf[50];
	    const int version		= it->endian->rd32(data+8);
	    const brsub_info_t *bi	= GetInfoBRSUB(fform,version);
	    const brsub_info_t *bic	= GetCorrectBRSUB(fform);
	    bool print_status		= true;
	    enumError err		= ERR_OK;

	    if (!bi)
	    {
		err = ERR_INVALID_DATA;
		snprintf(stat_buf,sizeof(stat_buf),
			"%sUnknown%s", colout->fatal, colout->reset );
	    }
	    else
	    {
		switch(bi->warn)
		{
		    case BIMD_OK:
			err = ERR_INVALID_DATA;
			print_status = cb->mode > 1;
			snprintf(stat_buf,sizeof(stat_buf),
				"%sOk     %s", colout->success, colout->reset );
			break;

		    case BIMD_INFO:
		    case BIMD_HINT:
			err = ERR_INVALID_VERSION;
			print_status = cb->mode > 0;
			snprintf(stat_buf,sizeof(stat_buf),
				"%sUnusual%s", colout->hint, colout->reset );
			break;

		    case BIMD_FAIL:
			err = ERR_INVALID_DATA;
			snprintf(stat_buf,sizeof(stat_buf),
				"%sFail   %s", colout->warn, colout->reset );
			break;

		    default:
		    //case BIMD_FATAL:
			err = ERR_INVALID_DATA;
			snprintf(stat_buf,sizeof(stat_buf),
				"%sFreeze %s", colout->bad, colout->reset );
			break;
		}
	    }

	    if ( cb->max_err < err )
		 cb->max_err = err;

	    if (print_status)
	    {
		if ( bic && version != bic->good_version )
		    snprintf(vers_buf,sizeof(vers_buf),
			    "%s%3d!%s", colout->hint, version, colout->reset );
		else
		    snprintf(vers_buf,sizeof(vers_buf), "%3d ", version );

		const int n_sect = GetGenericSectionNumBRSUB(data,it->size,it->endian);
		if ( !bic || n_sect != bic->good_sect )
		    snprintf(ns_buf,sizeof(ns_buf),
			    "%s%2d!%s", colout->fatal, n_sect, colout->reset );
		else
		    snprintf(ns_buf,sizeof(ns_buf), "%2d ", n_sect );

		if (!cb->header_printed)
		{
		    cb->header_printed = true;
		    cb->line_count = 999;
		    if (print_header)
			printf( "\n%s%s:%s%s\n"
			    "%s%.*s\n"
			    "%s Status  Type Ver N/s Filename of BRRES and sub file%s\n",
			    colout->caption, GetNameFF_SZS(cb->szs),
			    cb->szs->fname, colout->reset,
			    colout->heading, cb->sep_fw, ThinLine300_3,
			    colout->heading, colout->reset );
		    else
			printf("\n%s>%s:%s%s\n",
			    colout->caption, GetNameFF_SZS(cb->szs),
			     cb->szs->fname, colout->reset );
		}

		if ( cb->print_sep && cb->line_count > 2 )
		{
		    cb->print_sep = false;
		    cb->line_count = 0;
		    printf("%s%.*s%s\n",
			colout->heading, cb->sep_fw, ThinLine300_3, colout->reset );
		}

		cb->sep_count++;
		cb->line_count++;
		printf(" %s %-3s %s %s %s / %s\n",
			stat_buf, GetNameFF(0,bic?bic->fform1:fform),
			vers_buf, ns_buf,
			cb->brres_path, it->path );
	    }
	}
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

enumError cmd_brsub()
{
    if (!n_param)
    {
	const int fw = 3*34;
	if (print_header)
	    printf( "\n"
		"%s%.*s\n"
		"%s file format vers n(sect) warning%s\n",
		colout->heading, fw, ThinLine300_3,
		colout->heading, colout->reset );

	file_format_t last_ff = 0;
	const brsub_info_t *bi;
	for ( bi = brsub_info; bi->fform1; bi++ )
	{
	    if ( print_header && last_ff != bi->fform1 )
	    {
		last_ff = bi->fform1;
		if (print_header)
		    printf("%s%.*s%s\n",
			colout->heading, fw, ThinLine300_3, colout->reset );
	    }

	    printf(" %-3s %-7s %3d %6u   ",
			GetNameFF(0,bi->fform1),
			bi->fform2 ? GetNameFF(0,bi->fform2) : "-",
			bi->version, bi->n_sect );
	    switch(bi->warn)
	    {
		case BIMD_OK:
		    fputs("-\n",stdout);
		    break;

		case BIMD_INFO:
		case BIMD_HINT:
		    printf("%sUNUSUAL%s\n",colout->hint,colout->reset);
		    break;

		case BIMD_FAIL:
		    printf("%sFAIL%s\n",colout->warn,colout->reset);
		    break;

		default:
		//case BIMD_FATAL:
		    printf("%sFREEZE%s\n",colout->bad,colout->reset);
		    break;
	    }
	}

	if ( print_header )
	    printf("%s%.*s%s\n\n",
			colout->heading, fw, ThinLine300_3, colout->reset );

	return ERR_OK;
    }

    //--- have param => check BRSUB versions

    szs_file_t szs;
    InitializeSZS(&szs);
    uint line_count = 0;
    enumError max_err = ERR_OK;

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	ResetSZS(&szs);
	enumError err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,true);

	if ( err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore )
	    continue;
	if ( err > ERR_WARNING )
	    return err;
	if ( max_err < err )
	     max_err = err;

	check_brsub_t cb;
	memset(&cb,0,sizeof(cb));
	cb.szs    = &szs;
	cb.mode   = long_count;
	cb.sep_fw = 3*99;
	IterateFilesParSZS(&szs,check_brsub,&cb,false,false,false,-1,-1,SORT_NONE);
	line_count += cb.line_count;

	if ( print_header && cb.header_printed )
	    printf("%s%.*s%s\n",
			colout->heading, cb.sep_fw, ThinLine300_3, colout->reset );

	if ( max_err < cb.max_err )
	     max_err = cb.max_err;
    }

    if (line_count)
	putchar('\n');

    ResetStringField(&plist);
    ResetSZS(&szs);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command list			///////////////
///////////////////////////////////////////////////////////////////////////////

static int list_func
(
    struct szs_iterator_t	*it,	// iterator struct with all infos
    bool			term	// true: termination hint
)
{
    DASSERT(it);
    DASSERT(it->szs);
    if (term)
	return 0;

    ccp col_reset = "";
    if (colorize_stdout)
    {
	if (it->is_dir)
	{
	    fputs(colset->cmd,stdout);
	    col_reset = colset->reset;
	}
	else if ( it->has_subfiles )
	{
	    fputs(colset->heading,stdout);
	    col_reset = colset->reset;
	}
    }

    FixIteratorExt(it);
    const int indent = 2 * it->recurse_level;

    if ( long_count > 1 )
    {
	if (it->is_dir)
	    printf("      -       -        -  -      -  %.*s%s%s\n",
			indent, indent_msg, it->path, col_reset );
	else
	{
	    char vers_buf[50];
	    PrintVersionSZS(vers_buf,sizeof(vers_buf),it);

	    const u8 * data = it->szs->data + it->off;
	    printf("%7x %7x %8u  %-4s %s %.*s%s%s\n",
			it->off, it->size, it->size,
			PrintID(data, it->size < 4 ? it->size : 4, 0 ),
			vers_buf,
			indent,indent_msg, it->path, col_reset );
	}
    }
    else if ( long_count > 0 )
    {
	if (it->is_dir)
	    printf("       -  -     %.*s%s%s\n",
			indent,indent_msg, it->path, col_reset );
	else
	    printf("%8u  %-4s  %.*s%s%s\n",
			it->size,
			PrintID( it->szs->data+it->off, it->size < 4 ? it->size : 4, 0 ),
			indent,indent_msg, it->path, col_reset );
    }
    else
	printf("%.*s%s%s\n",indent,indent_msg,it->path,col_reset);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////

static enumError list_sarc_file ( ccp arg )
{
    u8 *data = 0;
    size_t file_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&data,&file_size,0,0,0,false);
    if (err) return err;
    if (file_size > UINT_MAX || DetectNintendoFormat(data,file_size,arg).type != NFMT_SARC)
        { FREE(data); return ERR_NOTHING_TO_DO; }
    nintendo_sarc_t sarc;
    err = ScanSARC(&sarc,data,file_size);
    if (err) { FREE(data); return err; }
    if (print_header)
        printf("\n%s> %u file%s of %s-endian SARC:%s %s\n",
            colout->stat_line,sarc.n_entries,sarc.n_entries==1?"":"s",
            sarc.big_endian ? "big" : "little",arg,colout->reset);
    for (uint i = 0; i < sarc.n_entries; i++)
    {
        ccp name;
        const u8 *entry;
        uint size;
        err = GetSARCEntry(&sarc,i,&name,&entry,&size);
        if (err) break;
        if (long_count) printf("%8u  %-4s %s\n",size,PrintID(entry,size<4?size:4,0),name);
        else puts(name);
    }
    FREE(data);
    return err;
}

static enumError list_ncer_file ( ccp arg )
{
    u8 *data = 0;
    size_t file_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&data,&file_size,0,0,0,false);
    if (err) return err;
    if (file_size > UINT_MAX || DetectNintendoFormat(data,file_size,arg).type != NFMT_NCER)
        { FREE(data); return ERR_NOTHING_TO_DO; }
    nintendo_ncer_t ncer;
    err = ScanNCER(&ncer,data,file_size);
    if (err) { FREE(data); return ERR_INVALID_DATA; }
    if (print_header)
        printf("\n%s> %u cell%s of NCER:%s %s\n",colout->stat_line,ncer.n_cells,
            ncer.n_cells == 1 ? "" : "s",colout->reset,arg);
    for (uint i = 0; i < ncer.n_cells; i++)
    {
        uint n_obj = 0;
        const u8 *oam = 0;
        err = GetNCERCell(&ncer,i,&n_obj,&oam);
        if (err) break;
        printf("cell %u: %u OBJ%s\n",i,n_obj,n_obj == 1 ? "" : "s");
        if (long_count)
            for (uint j = 0; j < n_obj; j++, oam += 6)
            {
                const u16 a0 = le16(oam), a1 = le16(oam+2), a2 = le16(oam+4);
                const int x = (a1 & 0x100) ? (a1&0x1ff)-0x200 : a1&0x1ff;
                const int y = (a0 & 0x80) ? (a0&0xff)-0x100 : a0&0xff;
                printf("  obj %-3u xy=(%d,%d) tile=%u shape=%u size=%u attr=%04x/%04x/%04x\n",
                    j,x,y,a2&0x3ff,a0>>14,a1>>14,a0,a1,a2);
            }
    }
    FREE(data);
    return err;
}

static enumError list_nanr_file ( ccp arg )
{
    u8 *data = 0;
    size_t file_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&data,&file_size,0,0,0,false);
    if (err) return err;
    if (file_size > UINT_MAX || DetectNintendoFormat(data,file_size,arg).type != NFMT_NANR)
        { FREE(data); return ERR_NOTHING_TO_DO; }
    nintendo_nanr_t nanr;
    err = ScanNANR(&nanr,data,file_size);
    if (err) { FREE(data); return ERR_INVALID_DATA; }
    if (print_header)
        printf("\n%s> %u animation%s, %u frame%s of NANR:%s %s\n",colout->stat_line,
            nanr.n_animations,nanr.n_animations == 1 ? "" : "s",nanr.n_frames,
            nanr.n_frames == 1 ? "" : "s",colout->reset,arg);
    for (uint i = 0; i < nanr.n_animations; i++)
    {
        uint n_frames = 0;
        const u8 *frames = 0;
        err = GetNANRAnimation(&nanr,i,&n_frames,&frames);
        if (err) break;
        printf("animation %u: %u frame%s\n",i,n_frames,n_frames == 1 ? "" : "s");
        if (long_count)
            for (uint j = 0; j < n_frames; j++, frames += 8)
            {
                const uint data_off = le32(frames);
                printf("  frame %-3u cell=%u duration=%u data=%x\n",j,
                    le16(nanr.frame_data+data_off),le16(frames+4),data_off);
            }
    }
    FREE(data);
    return err;
}

static enumError list_brfnt_file ( ccp arg )
{
    u8 *data = 0;
    size_t file_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&data,&file_size,0,0,0,false);
    if (err) return err;
    const nfmt_type_t type = file_size <= UINT_MAX ? DetectNintendoFormat(data,file_size,arg).type : NFMT_UNKNOWN;
    if (type != NFMT_BRFNT && type != NFMT_BRFNA)
        { FREE(data); return ERR_NOTHING_TO_DO; }
    if (file_size < 0x10 || (data[4] != 0xfe || data[5] != 0xff))
        { FREE(data); return ERR_INVALID_DATA; }
    const uint declared = be32(data+8), n_sections = be16(data+14);
    if (declared > file_size || !n_sections) { FREE(data); return ERR_INVALID_DATA; }
    if (print_header)
        printf("\n%s> %s font with %u section%s:%s %s\n",colout->stat_line,
            GetNintendoFormatName(type),n_sections,n_sections == 1 ? "" : "s",colout->reset,arg);
    uint off = 0x10;
    for (uint i = 0; i < n_sections; i++)
    {
        if (off > declared-8) { err = ERR_INVALID_DATA; break; }
        const uint size = be32(data+off+4);
        if (size < 8 || size > declared-off) { err = ERR_INVALID_DATA; break; }
        printf("section %-2u %-4s size=%x",i,PrintID(data+off,4,0),size);
        if (!memcmp(data+off,"TGLP",4) && size >= 0x20)
            printf(" glyph=%ux%u sheets=%u format=%u cells=%ux%u image=%ux%u",
                data[off+8],data[off+9],be16(data+off+0x10),be16(data+off+0x12),
                be16(data+off+0x14),be16(data+off+0x16),be16(data+off+0x18),be16(data+off+0x1a));
        putchar('\n');
        off += size;
    }
    FREE(data);
    return err;
}

static enumError cmd_list ( int long_level )
{
    SetupPager();
    SetPatchFileModeReadonly();

    if ( opt_recurse < 0 )
	 opt_recurse = 0;

    if ( long_level > 0 )
    {
	RegisterOptionByIndex(&InfoUI_wszst,OPT_LONG,long_level,false);
	long_count += long_level;
    }

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];
	enumError sarc_err = list_sarc_file(arg);
	if (sarc_err != ERR_NOTHING_TO_DO)
	{
	    if (sarc_err > ERR_WARNING) { ResetStringField(&plist); return sarc_err; }
	    continue;
	}
	enumError ncer_err = list_ncer_file(arg);
	if (ncer_err != ERR_NOTHING_TO_DO)
	{
	    if (ncer_err > ERR_WARNING) { ResetStringField(&plist); return ncer_err; }
	    continue;
	}
	enumError nanr_err = list_nanr_file(arg);
	if (nanr_err != ERR_NOTHING_TO_DO)
	{
	    if (nanr_err > ERR_WARNING) { ResetStringField(&plist); return nanr_err; }
	    continue;
	}
	enumError font_err = list_brfnt_file(arg);
	if (font_err != ERR_NOTHING_TO_DO)
	{
	    if (font_err > ERR_WARNING) { ResetStringField(&plist); return font_err; }
	    continue;
	}

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,true);
	if ( err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore )
	    continue;
	if ( err > ERR_WARNING )
	    return err;

	CollectFilesSZS(&szs,true,-1,-1,SORT_NONE);
	if ( opt_norm || need_norm > 0 )
	    NormalizeSZS(&szs);

	if (print_header)
	    printf("\n%s> %u director%s + %u file%s of %s:%s %s\n",
		colout->stat_line,
		szs.n_dirs, szs.n_dirs == 1 ? "y" : "ies",
		szs.n_files, szs.n_files == 1 ? "" : "s",
		GetNameFF_SZS(&szs), arg,
		colout->reset );

	int fw_name = szs.fw_files, seplen = 0;
	if ( long_count >= 3 )
	    fw_name++;
	if ( fw_name < 17 )
	    fw_name = 17;

	if (print_header)
	{
	    static ccp f_or_d = "file or directory";
	    if ( long_count > 2 )
		seplen = PrintFileHeadSZS(fw_name) * 3;
	    else if ( long_count > 1 )
	    {
		seplen = ( fw_name + 37 ) * 3;
		printf("\n%soff/hex siz/hex size/dec magic vers %s\n%s%.*s%s\n",
			colout->heading, f_or_d,
			colout->heading, seplen, ThinLine300_3, colout->reset );
	    }
	    else if ( long_count > 0 )
	    {
		seplen = ( fw_name + 17 ) * 3;
		printf("\n%ssize/dec  magic %s\n%s%.*s%s\n",
			colout->heading, f_or_d,
			colout->heading, seplen, ThinLine300_3, colout->reset );
	    }
	    else
		putchar('\n');
	}

	if ( long_count > 2 )
	    IterateFilesParSZS(&szs,PrintFileSZS,0,false,true,false,opt_recurse,opt_cut,opt_sort);
	else
	    IterateFilesParSZS(&szs,list_func,0,false,false,false,opt_recurse,opt_cut,opt_sort);

	if (print_header)
	{
	    if ( seplen && szs.n_dirs + szs.n_files > 2 )
		printf("%s%.*s%s\n\n", colout->heading, seplen, ThinLine300_3, colout->reset );
	    else
		putchar('\n');
	}
	ResetSZS(&szs);
    }

    ResetStringField(&plist);
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command name-ref		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_name_ref()
{
    stdlog = stderr;
    const SortMode_t std_sort = opt_sort == SORT_INAME ? SORT_INAME : SORT_BRRES;

    raw_data_t raw;
    InitializeRawData(&raw);

    enumError cmd_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];
	enumError err = LoadRawData(&raw,false,arg,
					"course_model.brres",opt_ignore>0,0);
	if ( err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore )
	    continue;
	if ( err > ERR_WARNING )
	{
	    ResetRawData(&raw);
	    return err;
	}

	if ( raw.fform != FF_BRRES && !IsBRSUB(raw.fform) )
	{
	    printf("No BRRES family -> ignore %s:%s\n",
			GetNameFF(raw.fform,0), raw.fname );
	    continue;
	}

	szs_file_t szs;
	AssignSZS(&szs,true,raw.data,raw.data_size,false,raw.fform,raw.fname);
	raw.fname = NULL;

	name_ref_t nr;
	err = CreateNameRef(&nr,true,&szs,brief_count);
	if (err)
	    return err;

	if ( long_count > 0 )
	{
	    ListNameRef( stdout,0, &nr, std_sort );
	    ListNameRef( stdout,0, &nr, SORT_OFFSET );
	    if ( long_count > 1 )
		ListNameRef( stdout,0, &nr, SORT_NONE );
	}
	else
	    ListNameRef( stdout,0, &nr, opt_sort );

	ResetNameRef(&nr);
    }

    ResetStringField(&plist);
    ResetRawData(&raw);
    return cmd_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command ilist			///////////////
///////////////////////////////////////////////////////////////////////////////

static int ilist_func
(
    struct szs_iterator_t	*it,	// iterator struct with all infos
    bool			term	// true: termination hint
)
{
    DASSERT(it);
    DASSERT(it->szs);
    if (term)
	return 0;

    if (!it->is_dir)
    {
	const u8 * data = it->szs->data+it->off;
// [[analyse-magic]]
	PrintImage( data, it->size, it->path,
			0, 2 * it->recurse_level, long_count,
			!it->has_subfiles
				|| GetByMagicFF(data,it->size,it->size) == FF_BREFF );
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_ilist ( int long_level )
{
    SetPatchFileModeReadonly();

    if ( opt_recurse < 0 )
	 opt_recurse = 0;

    if ( long_level > 0 )
    {
	RegisterOptionByIndex(&InfoUI_wszst,OPT_LONG,long_level,false);
	long_count += long_level;
    }

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,true);
	if ( err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore )
	    continue;
	if ( err > ERR_WARNING )
	    return err;

	if ( opt_norm || need_norm > 0 )
	    NormalizeSZS(&szs);

	if (print_header)
	    PrintImageHead(0,long_count);
	else if ( n_param > 1 )
	    printf("\n* Files of %s\n",arg);

	IterateFilesParSZS(&szs,ilist_func,0,false,false,false,-1,0,opt_sort);
	ResetSZS(&szs);
    }

    if (print_header)
	putchar('\n');

    ResetStringField(&plist);
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command memory			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct dumpmem_t
{
    szs_file_t		* szs;		// the container
    u8			* data_base;	// pointer to base of complete file
    uint		last_off;	// offset of last analyzed data
    uint		last_end;	// end of last analyzed data
    bool		print_abs_off;	// true: print absolute offset

} dumpmem_t;

///////////////////////////////////////////////////////////////////////////////

static void DumpUnused
(
    dumpmem_t		* dm,		// dump info
    uint		offset,		// offset of object to analyze and dump
    file_format_t	fform,		// NULL for last entry or fileformat
    uint		recurse_level	// recurse level
)
{
    DASSERT(dm);

    recurse_level *= 2;
    if ( fform != FF_INVALID )
    {
	if (dm->print_abs_off)
	    printf("%7x ",offset);

	ccp ff = GetNameFF(fform,0);

	if ( dm->last_end < offset )
	    printf("%*s%-6.6s %6x ",
			recurse_level, "",
			ff,offset - dm->last_end );
	else if ( dm->last_end > offset )
	    printf("%*s%-6.6s %6x-",
			recurse_level, "",
			ff, dm->last_end - offset );
	else
	    printf("%*s%-6.6s      - ",
			recurse_level, "",
			ff );
    }
    else if ( dm->last_end < offset )
    {
	if (dm->print_abs_off)
	    printf("%7x ",offset);

	printf("%*s-      %6x\n",
			recurse_level, "",
			offset - dm->last_end);
    }
    else if ( dm->last_end > offset )
    {
	if (dm->print_abs_off)
	    printf("%7x ",offset);

	printf("%*s-      %6x-\n",
			recurse_level, "",
			dm->last_end - offset);
    }

    dm->last_off = dm->last_end = offset;
}

///////////////////////////////////////////////////////////////////////////////

static int memory_func
(
    struct szs_iterator_t	*it,	// iterator struct with all infos
    bool			term	// true: termination hint
)
{
    DASSERT(it);
    DASSERT(it->szs);
    DASSERT(it->param);

    dumpmem_t *dm = it->param;

    if (term)
    {
	const uint abs_off = it->szs->data - dm->data_base + it->szs->size;
	noPRINT("TERM: %x %x %x\n",dm->last_off,dm->last_end,abs_off);
	DumpUnused(dm,abs_off,FF_INVALID,it->recurse_level);
    }
    else if (!it->is_dir)
    {
	if (!it->client_int)
	{
	    noPRINT("LAST: %x %x\n",dm->last_off, dm->last_end);
	    it->client_int++;
	    dm->last_end = dm->last_off;
	}

	const u8 *data = it->szs->data + it->off;
// [[analyse-magic]]
	file_format_t fform = GetByMagicFF(data,it->size,it->size);
	const uint abs_off = it->szs->data - dm->data_base + it->off;
	DumpUnused(dm,abs_off,fform,it->recurse_level);
	printf(" %6x .. %6x %6x  %s\n",
	    it->off, it->off + it->size, it->size, it->path );

	dm->last_off = abs_off;
	dm->last_end = abs_off + it->size;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_memory()
{
    SetPatchFileModeReadonly();

    if ( opt_recurse < 0 )
	 opt_recurse = 0;

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,true);
	if ( err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore )
	    continue;
	if ( err > ERR_WARNING )
	    return err;

	if ( opt_norm || need_norm > 0 )
	    NormalizeSZS(&szs);

	dumpmem_t dm;
	memset(&dm,0,sizeof(dm));
	dm.szs		 = &szs;
	dm.data_base	 = szs.data;
	dm.print_abs_off = long_count > 0;

	if (print_header)
	{
	    printf("\n* memory dump of %s:%s\n",
			GetNameFF_SZS(&szs), arg );

	    fputs( dm.print_abs_off ? "\nabs off " : "\n", stdout);
	    printf( "type    unused  begin ..    end   size  file name\n"
		    "%.80s\n",
		    Minus300 );
	}

	IterateFilesParSZS(&szs,memory_func,&dm,false,false,false,opt_recurse,opt_cut,SORT_OFFSET);
	ResetSZS(&szs);
    }

    if (print_header)
	putchar('\n');

    ResetStringField(&plist);
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command dump			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_dump()
{
    SetPatchFileModeReadonly();

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,true);
	if ( err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore )
	    continue;
	if ( err > ERR_WARNING )
	    return err;

	if ( opt_norm || need_norm > 0 )
	    NormalizeSZS(&szs);

	if ( szs.fform_arch == FF_BRRES || IsBRSUB(szs.fform_arch) )
	{
	    printf("\n* Dump structure of %s:%s\n",
			GetNameFF_SZS(&szs), arg );
	    DumpStructureBRRES(stdout,&szs);
	}
	else if (!opt_ignore)
	    ERROR0(ERR_WARNING,"No BRRES file -> ignored: %s\n",arg);
	ResetSZS(&szs);
    }
    putchar('\n');


    ResetStringField(&plist);
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command sha1			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_sha1()
{
    stdlog = stderr;
    enumError max_err = ERR_OK;
    SetPatchFileModeReadonly();
    SetupCoding64( opt_coding64, opt_db64 ? ENCODE_BASE64URL : ENCODE_BASE64 );

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,true);
	if ( max_err < err )
	    max_err = err;
	if ( err > ERR_WARNING || err == ERR_NOT_EXISTS )
	{
	    if ( opt_db64 )
	    {
		//fputs("--------------------------------",stdout);
		putchar('-');
		if (long_count)
		    fputs("  -1 -1 0.000  -",stdout);
		if (brief_count)
		    putchar('\n');
		else
		    printf("  %s\n",arg);
	    }

	    if ( err == ERR_NOT_EXISTS || opt_ignore )
		continue;
	    return err;
	}

	if ( opt_norm || need_norm > 0 || opt_rm_aiparam )
	    NormalizeSZS(&szs);

	char checksum[100];
	CreateSSChecksumBySZS(checksum,sizeof(checksum),&szs);

	if (opt_verify)
	{
	    const uint clen = strlen(checksum);
	    ccp fname = strrchr(arg,'/');
	    fname = fname ? fname+1 : arg;
	    bool found = false;
	    for(;;)
	    {
		ccp pos = strstr(fname,checksum);
		if (!pos)
		    break;

		if ( pos == arg || pos[-1] == '.' || pos[-1] == '-' )
		{
		    char ch = pos[clen];
		    if ( !ch || ch == '.' || ch == '-' )
		    {
			found = true;
			break;
		    }
		}

		fname = pos+1; // search more
	    }

	    if (!found)
	    {
		if ( max_err < ERR_DIFFER )
		    max_err = ERR_DIFFER;
		if ( verbose >= 0 )
		    printf("Checksum differ: %s\n",arg);
	    }
	    else if ( verbose > 0 )
		printf("Checksum ok:     %s\n",arg);
	}
	else
	{
	    if (!opt_db64)
	    {
		if (long_count>1)
		{
		    struct tm * tm = localtime(&szs.fatt.mtime.tv_sec);
		    char timbuf[40];
		    strftime(timbuf,sizeof(timbuf),"%F %T ",tm);
		    fputs(timbuf,stdout);
		}
		if (long_count)
		    printf("%9zu,",szs.size);
	    }

	    fputs(checksum,stdout);

	    if ( opt_db64 && long_count )
	    {
		int	ckpt0_count	= -1;		// number of LC in CKPT
		int	lap_count	= 3;		// STGI lap counter
		float	speed_factor	= 1.0;		// STGI speed factor
		ccp slot_info = CreateSlotInfo(&szs);

		FindSpecialFilesSZS(&szs,false);
		if (szs.course_kmp_data)
		{
		    kmp_t kmp;
		    InitializeKMP(&kmp);
		    err = ScanKMP(&kmp,false,szs.course_kmp_data,szs.course_kmp_size,0);
		    if ( err <= ERR_WARNING )
		    {
			const uint n_ckpt =  kmp.dlist[KMP_CKPT].used;
			if (n_ckpt)
			{
			    const kmp_ckpt_entry_t *ckpt
				= (kmp_ckpt_entry_t*)kmp.dlist[KMP_CKPT].list;
			    uint i;
			    ckpt0_count = 0;
			    for ( i = 0; i < n_ckpt; i++, ckpt++ )
				if (!ckpt->mode)
				    ckpt0_count++;
			}

			const kmp_stgi_entry_t *stgi = (kmp_stgi_entry_t*)kmp.dlist[KMP_STGI].list;
			if ( kmp.dlist[KMP_STGI].used > 0 )
			{
			    lap_count = stgi->lap_count;

			    if (stgi->speed_mod)
				speed_factor = SpeedMod2float(stgi->speed_mod);
			}
		    }
		    ResetKMP(&kmp);
		}

		printf("  %d %d %5.3f  %s",ckpt0_count,lap_count,speed_factor,slot_info);
	    }

	    if (brief_count)
		putchar('\n');
	    else
		printf("  %s\n", arg );
	}
	ResetSZS(&szs);
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command analyze			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_analyze()
{
    SetupPager();

    static ccp def_path = "\1P/\1F\1?T";
    CheckOptDest("-",false);
    char dest[PATH_MAX];
    enumError max_err = ERR_OK;
    if (!strcmp(opt_dest,"-"))
	fputc('\n',stdout);

    analyze_param_t ap = {0};
    InitializeSZS(&ap.szs);
    InitializeFile(&ap.fo);
    SetupPrintScriptByOptions(&ap.ps);

    patch_action_log_disabled++;
    CheckTextureRefSZS(&ap.szs,0);

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	ResetSZS(&ap.szs);
	enumError err = LoadCreateSZS(&ap.szs,arg,true,opt_ignore>0,true);
	if ( err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore )
	    continue;
	if ( err > ERR_WARNING )
	{
	    if ( max_err < err )
		max_err = err;
	    continue;
	}

	if (!ap.fo.f)
	    SubstDest(dest,sizeof(dest),arg,opt_dest,def_path,
			GetExtFF(script_fform,0),false);

	if ( verbose > 0 )
	{
	    fprintf(stdlog,"%sANALYZE %s:%s => %s:%s\n",
			verbose > 0 ? "\n" : "",
			GetNameFF(ap.szs.fform_file,ap.szs.fform_arch), ap.szs.fname,
			GetNameFF(script_fform,0), dest );
	    fflush(stdlog);
	}

	if ( err >= ERR_WARNING )
	{
	    if ( max_err < err )
		 max_err = err;
	    continue;
	}

	if (!ap.fo.f)
	{
	    enumError err = CreateFile(&ap.fo,false,dest,FM_STDIO|FM_OVERWRITE);
	    if (err)
	    {
		max_err = err;
		break;
	    }
	    ap.ps.f = ap.fo.f;
	    PrintScriptHeader(&ap.ps);
	}

	ap.fname	= arg;


	//--- analyze by file format

	switch(ap.szs.fform_arch)
	{
	    case FF_U8:
	    case FF_WU8:	err = ExecAnalyzeSZS(&ap); break;
	    case FF_LE_BIN:	err = ExecAnalyzeLECODE(&ap); break;

	    default:
		PrintHeaderAP(&ap,"");
		PrintFooterAP(&ap,false,0,"File format not supported.");
		break;
	}

	if (!script_array)
	{
	    PrintScriptFooter(&ap.ps);
	    ap.ps.f = 0;
	    ResetFile(&ap.fo,0);
	}
    }

    PrintScriptFooter(&ap.ps);

    ResetStringField(&plist);
    ResetPrintScript(&ap.ps);
    ResetFile(&ap.fo,0);
    ResetSZS(&ap.szs);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command split			///////////////
///////////////////////////////////////////////////////////////////////////////

static void print_split_val ( PrintScript_t *ps, ccp name, mem_t val )
{
    DASSERT(ps);
    DASSERT(name);


    if ( val.ptr && val.len )
    {
	char buf[1000];
	PrintEscapedString(buf,sizeof(buf),val.ptr,val.len,CHMD_UTF8,'"',0);
	PrintScriptVars(ps,0,"%s=\"%s\"\n",name,buf);
    }
    else
	PrintScriptVars(ps,0,"%s=\"\"\n",name);
}

//-----------------------------------------------------------------------------

static enumError cmd_split()
{
    static ccp def_path = "\1P/\1F\1?T";
    CheckOptDest("-",false);
    char dest[PATH_MAX];
    enumError max_err = ERR_OK;
    if ( opt_split <= 0 && !strcmp(opt_dest,"-") )
	fputc('\n',stdout);

    split_filename_t spf;
    InitializeSPF(&spf);

    print_split_par_t psp = { .format = opt_printf, .split_level = opt_split, .u_use_list = true };

    File_t fo;
    InitializeFile(&fo);

    PrintScript_t ps;
    SetupPrintScriptByOptions(&ps);

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	if (!fo.f)
	    SubstDest(dest,sizeof(dest),arg,opt_dest,def_path,
			GetExtFF(script_fform,0),false);

	if (!fo.f)
	{
	    enumError err = CreateFile(&fo,false,dest,FM_STDIO|FM_OVERWRITE);
	    if (err)
	    {
		max_err = err;
		break;
	    }
	    ps.f = fo.f;
	    PrintScriptHeader(&ps);
	}

	const u_nsec_t start_nsec = GetTimerNSec();
	AnalyseSPF(&spf,false,arg,0,CPM_LINK,opt_plus);
	const u_nsec_t duration_nsec = GetTimerNSec() - start_nsec;

	if ( opt_split > 0 )
	{
	    exmem_t res = PrintSPF(0,&spf,&psp);
	    printf("%s\n",res.data.ptr);
	    FreeExMem(&res);
	    continue;
	}

	PutScriptVars(&ps,1,0);

	print_split_val(&ps,"directory",spf.directory);
	print_split_val(&ps,"source",spf.source);

	print_split_val(&ps,"file_name",spf.f_name);
	print_split_val(&ps,"file_d",spf.f_d);
	print_split_val(&ps,"file_ext",spf.f_ext);

	print_split_val(&ps,"normed",spf.norm);
	print_split_val(&ps,"plus",spf.plus);
	PrintScriptVars(&ps,0,"plus_order=%d\n",spf.plus_order);
	print_split_val(&ps,"boost",spf.boost);

	print_split_val(&ps,"game1",spf.game1);
	print_split_val(&ps,"game2",spf.game2);
	print_split_val(&ps,"game",spf.game);
	PrintScriptVars(&ps,0,"game_order=%d\ngame1_color=%d\ngame2_color=%d\n",
				spf.game_order, spf.game1_color, spf.game2_color );

	print_split_val(&ps,"name",spf.name);
	print_split_val(&ps,"extra",spf.extra);
	print_split_val(&ps,"version",spf.version);
	print_split_val(&ps,"authors",spf.authors);
	print_split_val(&ps,"editors",spf.editors);
	print_split_val(&ps,"attribs",spf.attribs);
	PrintScriptVars(&ps,0,"attrib_order=%d\n",spf.attrib_order);

	PrintScriptVars(&ps,0,"distrib_flags=\"%u %s\"\n",spf.distrib_flags,PrintDTA(spf.distrib_flags,false));
	PrintScriptVars(&ps,0,"le_flags=\"%u %s\"\n",spf.le_flags,PrintLEFL8(spf.le_flags,false));
	print_split_val(&ps,"le_group",spf.le_group);

	const mkw_category_t *tcat = GetCategory(spf.track_cat,MKW_CAT_UNKNOWN);
	if (tcat)
	    PrintScriptVars(&ps,0,"track_cat=\"%u %s %u\"\n",spf.track_cat,tcat->name,tcat->mode);
	else
	    PrintScriptVars(&ps,0,"track_cat=\"%u - 0\"\n",spf.track_cat);

	PrintScriptVars(&ps,0,"lap_count=%u\n",spf.lap_count);
	if ( spf.speed_factor > 0.0 )
	    PrintScriptVars(&ps,0,"speed_factor=%5.3f\n",spf.speed_factor);
	else
	    PutScriptVars(&ps,0,"speed_factor=0\n");

	if (opt_printf)
	{
	    exmem_t res = PrintSPF(0,&spf,&psp);
	    print_split_val(&ps,"printf",res.data);
	    FreeExMem(&res);
	}

	PrintScriptVars(&ps,2,"duration_nsec=%llu\n",duration_nsec);

	fflush(ps.f);
	if (!script_array)
	{
	    PrintScriptFooter(&ps);
	    ps.f = 0;
	    ResetFile(&fo,0);
	}
    }

    PrintScriptFooter(&ps);

    ResetStringField(&plist);
    ResetPrintScript(&ps);
    ResetFile(&fo,0);
    ResetPSP(&psp);
    ResetSPF(&spf);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command is-texture		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_is_texture()
{
    disable_checks++;

    int max_err = 0;
    szs_file_t szs;
    InitializeSZS(&szs);
    patch_action_log_disabled++;

    uint max_fw = 0;
    struct { FastBuf_t b; char space[500]; } fbuf;
    if (long_count)
	InitializeFastBuf(&fbuf,sizeof(fbuf));

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	ResetSZS(&szs);
	enumError err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,true);
	if ( err > ERR_WARNING )
	{
	    if (long_count)
	    {
		AppendFastBuf(&fbuf.b,"-1=err",7);
		if ( max_fw < 7 )
		     max_fw = 7;
	    }
	    if ( max_err < err )
		max_err = err;
	    continue;
	}

	ccp texture_info = 0;
	PrepareCheckTextureSZS(&szs);
	CheckTextureRefSZS(&szs,&texture_info);

	if (long_count)
	{
	    ccp x = texture_info ? texture_info : "-1";
	    const int len = strlen(x);
	    AppendFastBuf(&fbuf.b,x,len+1);
	    if ( max_fw < len )
		 max_fw = len;
	}
	else
	    printf("%s\n", texture_info ? texture_info : "-1" );
    }

    if (long_count)
    {
	ccp par = fbuf.b.buf;
	for ( int argi = 0; argi < plist.used; argi++ )
	{
	    printf("%-*s : %s\n",max_fw,par,plist.field[argi]);
	    par += strlen(par) + 1;
	}
	ResetFastBuf(&fbuf.b);
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command features		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_features()
{
    static ccp def_path = "\1P/\1F\1?T";
    CheckOptDest("-",false);
    if (!strcmp(opt_dest,"-"))
	SetupPager();

    char dest[PATH_MAX];
    enumError max_err = ERR_OK;
    if (!strcmp(opt_dest,"-"))
	fputc('\n',stdout);

    szs_file_t szs;
    InitializeSZS(&szs);

    analyze_szs_t as;
    InitializeAnalyzeSZS(&as);

    features_szs_t fs;
    InitializeFeaturesSZS(&fs);
    const int comments = brief_count > 0 ? -brief_count : long_count;

    File_t fo;
    InitializeFile(&fo);

    PrintScript_t ps;
    SetupPrintScriptByOptions(&ps);
    ps.eq_tabstop = 3;
    if ( ps.fform == PSFF_UNKNOWN && comments >= 0 )
	ps.fform = PSFF_ASSIGN;

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	ResetSZS(&szs);
	enumError err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,true);
	if ( err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore )
	    continue;
	if ( err > ERR_WARNING )
	{
	    if ( max_err < err )
		max_err = err;
	    continue;
	}

	if (!fo.f)
	    SubstDest(dest,sizeof(dest),arg,opt_dest,def_path,
			GetExtFF(script_fform,0),false);

	if ( verbose > 0 )
	{
	    fprintf(stdlog,"%sANALYZE %s:%s => %s:%s\n",
			verbose > 0 ? "\n" : "",
			GetNameFF(szs.fform_file,szs.fform_arch), szs.fname,
			GetNameFF(script_fform,0), dest );
	    fflush(stdlog);
	}


	//--- analyse szs

	AnalyzeSZS(&as,false,&szs,arg);
	SetupFeaturesSZS(&fs,&as.have,false);


	//--- status

	if ( err >= ERR_WARNING )
	{
	    if ( max_err < err )
		 max_err = err;
	    continue;
	}

	if (!fo.f)
	{
	    enumError err = CreateFile(&fo,false,dest,FM_STDIO|FM_OVERWRITE);
	    if (err)
	    {
		max_err = err;
		break;
	    }
	    ps.f = fo.f;
	    PrintScriptHeader(&ps);
	}


	//--- name attributes ('dest' can be used now)

	PrintEscapedString(dest,sizeof(dest),arg,-1,CHMD_UTF8,'"',0);


	//--- print result

	PrintScriptVars(&ps,1,"file=\"%s\"\n",dest);
	PrintFeaturesSZS( &ps, &fs, false, comments, 1,
				long_count, opt_fmodes_include, opt_fmodes_exclude );
	PutScriptVars(&ps,2,0);

	if (!script_array)
	{
	    PrintScriptFooter(&ps);
	    ps.f = 0;
	    ResetFile(&fo,0);
	}
    }

    PrintScriptFooter(&ps);

    ResetStringField(&plist);
    ResetPrintScript(&ps);
    ResetFile(&fo,0);
    ResetFeaturesSZS(&fs);
    ResetAnalyzeSZS(&as);
    ResetSZS(&szs);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command distribution		///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ScanDistribFile
	( DistributionInfo_t *dinf, ccp fname, int ignore, bool assume_arena )
{
    DASSERT(dinf);
    if ( ignore && IsDirectory(fname,true) )
	return ERR_OK;

    szs_file_t szs;
    InitializeSZS(&szs);
    enumError err = LoadSZS(&szs,fname,true,ignore>0,true);

    if ( err > ERR_WARNING || err == ERR_NOT_EXISTS )
    {
	ResetSZS(&szs);
	return err == ERR_NOT_EXISTS || ignore ? ERR_OK : err;
    }

// [[lta]] [[2do]]
// [[lfl]] [[2do]]
    if ( szs.fform_arch != FF_U8 && szs.fform_arch != FF_WU8 )
    {
	ResetSZS(&szs);
	if ( ignore < 2 )
	    return ERROR0(ERR_INVALID_DATA,"Not a SZS ot WBZ file: %s\n",fname);
	return ERR_OK;
    }

    if ( opt_norm || need_norm > 0 )
	NormalizeExSZS(&szs,false,false,false);

    if ( verbose > 0 )
	printf("Read %s\n",fname);


    //--- cut filename

    char fname_buf[1000];

    ccp slash = strrchr(fname,'/');
    if (slash)
	fname = slash+1;

    StringCopyS(fname_buf,sizeof(fname_buf),fname);
    fname = fname_buf;

    char *src, *dest = strrchr(fname_buf,'.');
    if ( dest && strlen(dest) <= 4 )
	*dest = 0;

    src = dest = fname_buf;
    while (*src)
    {
	if (!memcmp(src,"ː",2))
	{
	    src += 2;
	    *dest++ = ':';
	}
	else if (!memcmp(src,"%3a",3))
	{
	    src += 3;
	    *dest++ = ':';
	}
	else
	    *dest++ = *src++;
    }
    *dest = 0;


    //--- create checksum

    char checksum[100];
    CreateSSChecksumBySZS(checksum,sizeof(checksum),&szs);
    ResetSZS(&szs);


    //--- save result

    uint slot = FindSlotByTranslation(&dinf->translate,fname,checksum);
    if (slot)
    {
	uint num;
	char *next = ScanSlot(&num,fname,false);
	if ( num == slot && (uchar)*next <= ' ' )
	    fname = next;
	else
	{
	    num = strtoul(fname,&next,10);
	    if ( num == slot && (uchar)*next <= ' ' )
		fname = next;
	}

	while ( *fname == ' ' )
	    fname++;
    }

    if (assume_arena)
	slot += DISTRIBUTION_ARENA_DELTA;

    PRINT(">>> slot=%d[%d,%d] %s\n",
		slot, IsValidDistribArena(slot), IsValidDistribTrack(slot), fname );

    if (IsValidDistribTrack(slot))
	AppendParamField(dinf->track+slot,fname,false,slot,STRDUP(checksum));
    else if (IsValidDistribArena(slot))
	AppendParamField(dinf->arena+slot-DISTRIBUTION_ARENA_DELTA,
				fname,false,slot,STRDUP(checksum));
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_distribution()
{
    SetPatchFileModeReadonly();
    SetupCoding64( opt_coding64, opt_db64 ? ENCODE_BASE64URL : ENCODE_BASE64 );

    DistributionInfo_t dinf;
    InitializeDistributionInfo(&dinf,true);

    char slotname[20];


    //--- define result file

    if ( !first_param || !first_param->arg || !*first_param->arg )
	return ERROR0(ERR_MISSING_PARAM,
		"Definition of result file missed (first parameter)!\n" );

    ParamList_t *param;
    char result[PATH_MAX], path[PATH_MAX];

    if (IsDirectory(first_param->arg,false))
    {
	PathCatPP(result,sizeof(result),first_param->arg,DEFAULT_DISTIBUTION_FNAME);
	param = first_param;
    }
    else
    {
	char *dest = StringCopyS(result,sizeof(result)-4,first_param->arg);
	if ( dest < result+4 || strcasecmp(dest-4,".txt") )
	    strcpy(dest,".txt");
	param = first_param->next;
    }


    //--- setup slot translation

    int i;
    for ( i = 0; i < MKW_N_ARENAS; i++ )
    {
	const TrackInfo_t *ti = arena_info+i;
	DefineSlotTranslation(&dinf.translate,true,ti->def_slot,ti->track_fname);
	DefineSlotTranslation(&dinf.translate,true,ti->def_slot,ti->name_en);
	DefineSlotTranslation(&dinf.translate,true,ti->def_slot,ti->name_de);
    }

    for ( i = 0; i < MKW_N_TRACKS; i++ )
    {
	const TrackInfo_t *ti = track_info+i;
	DefineSlotTranslation(&dinf.translate,false,ti->def_slot,ti->track_fname);
	DefineSlotTranslation(&dinf.translate,false,ti->def_slot,ti->name_en);
	DefineSlotTranslation(&dinf.translate,false,ti->def_slot,ti->name_de);
    }

    bool result_found = false;
    for ( i = 0; i < source_list.used; i++ )
    {
	ccp arg = source_list.field[i];
	if ( !i && !strcmp(arg,"0") )
	    ResetParamField(&dinf.translate);
	else if ( !result_found && !strcmp(result,arg) )
	{
	    result_found = true;
	    ScanSlotTranslation(&dinf.translate,&dinf.ld,arg,false);
	}
	else
	    ScanSlotTranslation(&dinf.translate,0,arg,false);
    }

    if (!result_found)
	ScanSlotTranslation(&dinf.translate,&dinf.ld,result,true);

    AddParamDistributionInfo(&dinf,false);


    //--- logging

    if ( logging >= 1 )
    {
	fputs("---------------  translation  ---------------\n",stdlog);

	uint next_slot = 0;
	while ( next_slot < INT_MAX )
	{
	    uint slot =  next_slot;
	    next_slot = INT_MAX;

	    uint i;
	    const ParamFieldItem_t *it = dinf.translate.field;
	    for ( i = 0; i < dinf.translate.used; i++, it++ )
	    {
		if ( it->num == slot )
		{
		    if ( slot < DISTRIBUTION_ARENA_DELTA )
			fprintf(stdlog,"%4u. %3u.%u  %s\n",
			    i+1, it->num/10, it->num%10, it->key );
		    else
		    {
			snprintf( slotname, sizeof(slotname), "A%u.%u",
				(it->num-DISTRIBUTION_ARENA_DELTA)/10,
				it->num%10 );
			fprintf(stdlog,"%4u. %5s  %s\n", i+1, slotname, it->key );
		    }
		}
		else if ( it->num > slot && it->num < next_slot )
		    next_slot = it->num;
	    }
	}

	fputs("----------------  parameter  ----------------\n",stdlog);

	uint i;
	const ParamFieldItem_t *it = dinf.ld.dis_param.field;
	for ( i = 0; i < dinf.ld.dis_param.used; i++, it++ )
	    fprintf(stdlog,"%4u. %-20s = %s\n",
			i+1, it->key, (ccp)it->data );

	fputs("---------------------------------------------\n",stdlog);
    }


    //--- iterate files & directories

    if ( verbose == 0 )
	printf("Scan SZS files\n");

    StringField_t plist = {0};
    CollectExpandParam(&plist,param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	bool assume_arena = false;
	ccp ptr = arg;
	for(;;)
	{
	    ptr = strchr(ptr,'/');
	    if (!ptr)
		break;
	    ptr++;
	    if (!strncasecmp(ptr,"arena",5))
	    {
		assume_arena = true;
		PRINT("ASSUME ARENA: %s\n",arg);
		break;
	    }
	}

	if (IsDirectory(arg,false))
	{
	    DIR * dir = opendir(arg);
	    if (dir)
	    {
		for(;;)
		{
		    struct dirent * dent = readdir(dir);
		    if (!dent)
			break;

		    if ( *dent->d_name != '.' )
		    {
			PathCatPP(path,sizeof(path),arg,dent->d_name);
 #if HAVE_WIIMM_EXT // [[2do]] by option
			if ( !strstr(path,",clan]") && !strstr(path,",head=") )
 #endif
			{
			    enumError err = ScanDistribFile(&dinf,path,2,assume_arena);
			    if (err)
				return err;
			}
		    }
		}
		closedir(dir);
	    }
	}
	else
	{
	    enumError err = ScanDistribFile(&dinf,arg,opt_ignore,assume_arena);
	    if (err)
		return err;
	}
    }


    //--- print results

    if ( verbose >= 0 )
	printf("Create file %s\n",result);

    StringCat2S(path,sizeof(path),result,".bak");
    rename(result,path);

    FILE *f = fopen(result,"wb");
    if (!f)
	ERROR1(ERR_CANT_CREATE,"Can't create file: %s\n",result);

    CreateDistribLD(f,&dinf.ld,true);


    //--- battle arenas

    int slot, slot10 = -1, count = 0;
    for ( slot = 0; slot < MAX_DISTRIBUTION_ARENA; slot++ )
    {
	ParamField_t *pf = dinf.arena + slot;
	if ( pf->used )
	{
	    count++;
	    if ( slot10 != slot/10 )
	    {
		slot10 = slot/10;
		fputs("\r\n",f);
	    }

	    if (slot)
		snprintf(slotname,sizeof(slotname),"A%u.%u",slot10,slot%10);
	    else
		StringCopyS(slotname,sizeof(slotname),"arena");

	    uint i;
	    ParamFieldItem_t *it = pf->field;
	    for ( i = 0; i < pf->used; i++, it++ )
		fprintf(f,"%s %5s  %s\r\n", (ccp)it->data, slotname, it->key );
	}
    }
    if (count)
	fputs("\r\n",f);


    //--- racing tracks


    slot10 = -1;
    count = 0;

    for ( slot = 0; slot < MAX_DISTRIBUTION_TRACK; slot++ )
    {
	ParamField_t *pf = dinf.track + slot;
	if ( pf->used )
	{
	    count++;
	    if ( slot10 != slot/10 )
	    {
		slot10 = slot/10;
		fputs("\r\n",f);
	    }

	    char slotname[10];
	    if (slot)
		snprintf(slotname,sizeof(slotname),"%3u.%u",slot10,slot%10);
	    else
		StringCopyS(slotname,sizeof(slotname),"  ---");

	    uint i;
	    ParamFieldItem_t *it = pf->field;
	    for ( i = 0; i < pf->used; i++, it++ )
		fprintf(f,"%s %s  %s\r\n", (ccp)it->data, slotname, it->key );
	}
    }
    if (count)
	fputs("\r\n",f);
    fclose(f);

    ResetStringField(&plist);
    ResetDistributionInfo(&dinf);
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command diff			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError diff_files ( ccp fname1, ccp fname2 )
{
    if ( verbose > 0 )
	printf("DIFF %s : %s\n", fname1, fname2 );

    szs_file_t szs1, szs2;
    InitializeSZS(&szs1);
    InitializeSZS(&szs2);

    enumError err = LoadCreateSZS(&szs1,fname1,true,false,true);
    if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
	err = LoadCreateSZS(&szs2,fname2,true,false,true);
    if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
	err = DiffSZS( &szs1, &szs2,
			opt_recurse < 0 ? 0 : opt_recurse,
			opt_cut ? 1 : -1,
			verbose < 0 );

    if ( verbose >= -1 && err == ERR_DIFFER )
	printf("Content differ: %s : %s\n", fname1, fname2 );
    else if ( verbose >= 0 && err == ERR_OK )
	printf("Content identical: %s : %s\n", fname1, fname2 );

    ResetSZS(&szs1);
    ResetSZS(&szs2);
    return err;
}

///////////////////////////////////////////////////////////////////////////////

static enumError diff_dest()
{
    enumError max_err = ERR_OK;
    ParamList_t *param;
    for ( param = first_param; param; param = param->next )
    {
	NORMALIZE_FILENAME_PARAM(param);

	char dest[PATH_MAX];
	SubstDest(dest,sizeof(dest),param->arg,opt_dest,"%F",0,false);
	const enumError err = diff_files(param->arg,dest);
	if ( max_err < err )
	     max_err = err;
	if ( err > ERR_WARNING || err && verbose < -1 )
	    break;
    }
    return max_err;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_diff()
{
    SetPatchFileModeReadonly();

    if (opt_dest)
	return diff_dest();

    if ( n_param != 2 )
	return ERROR0(ERR_SYNTAX,"Exact 2 sources expected if --dest is not set.\n");

    ASSERT(first_param);
    ASSERT(first_param->next);
    return diff_files(first_param->arg,first_param->next->arg);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command check			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_check()
{
    SetPatchFileModeReadonly();

    patch_action_log_disabled++;
    if ( KCL_MODE & KCLMD_DROP_AUTO )
	SetKclMode(KCL_MODE|KCLMD_DROP_UNUSED);

    enumError max_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,true);

	const bool check_verbose = verbose > 0 || long_count > 0;
	CheckMode_t mode = GetCheckMode(false,brief_count>0,verbose<0,check_verbose);
	if ( verbose >= 0 || testmode )
	{
	    ColorSet_t col;
	    SetupColorSet(&col,stdlog);
	    mode = GetCheckMode(true,brief_count>0,false,check_verbose);
	    fprintf(stdlog,"\n%sCHECK %s:%s%s\n",
			col.heading, GetNameFF_SZS(&szs), szs.fname, col.reset );
	    fflush(stdlog);
	}

	if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
	{
	    switch (szs.fform_arch)
	    {
		case FF_KCL:
		case FF_KCL_TXT:
		case FF_WAV_OBJ:
		case FF_SKP_OBJ:
		{
		    kcl_t kcl;
		    err = ScanKCL(&kcl,true,szs.data,szs.size,true,mode);
		    ResetKCL(&kcl);
		}
		break;

		case FF_KMP:
		case FF_KMP_TXT:
		{
		    kmp_t kmp;
		    err = ScanKMP(&kmp,true,szs.data,szs.size,mode);
		    ResetKMP(&kmp);
		}
		break;

		case FF_U8:
		case FF_WU8:
		    if (CheckSZS(&szs,mode,global_check_mode))
			err = ERR_DIFFER;
		break;

		case FF_BRRES:
		    if (CheckBRRES(&szs,mode,0))
			err = ERR_DIFFER;
		break;

		default:
		    ERROR0(ERR_INVALID_FFORM,
			"CHECK doesn't support file format '%s': %s\n",
			GetNameFF_SZS(&szs), arg );
		break;
	    }
	    fflush(stdout);
	}

	if ( max_err < err )
	     max_err = err;
	ResetSZS(&szs);
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command slot			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_slots()
{
    SetPatchFileModeReadonly();

    KCL_MODE |= KCLMD_SILENT;
    if ( KCL_MODE & KCLMD_DROP_AUTO )
	SetKclMode(KCL_MODE|KCLMD_DROP_UNUSED);

    const ColorMode_t colmode = GetFileColorized(stdout);
    ccp col_minus = GetTextMode(colmode,TTM_BOLD|TTM_RED);
    ccp col_plus  = GetTextMode(colmode,TTM_BOLD|TTM_GREEN);
    ccp col_misc  = GetTextMode(colmode,TTM_BOLD|TTM_CYAN);
    ccp col_reset = GetTextMode(colmode,TTM_RESET);

    enumError max_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,true);
	if (err)
	{
	    if ( max_err < err )
		 max_err = err;
	    ResetSZS(&szs);
	    continue;
	}
	if ( opt_norm || need_norm > 0 )
	    NormalizeSZS(&szs);
	//PatchSZS(&szs);

	int slot[MKW_N_TRACKS];
	int required_slot;
	const int stat = FindSlotsSZS(&szs,slot,&required_slot);

	noPRINT("IS_ARENA=%d\n",szs.is_arena);
	if ( szs.is_arena >= ARENA_FOUND )
	{
	    if (brief_count)
		printf("%sARENA%s     : %s\n",col_misc,col_reset,arg);
	    else
		printf("    %s ARENA %s     : %s\n",col_misc,col_reset,arg);
	}
	else
	{
	    if (brief_count)
	    {
		uint i, count = 0;
		for ( i = 0; i < MKW_N_TRACKS; i++ )
		    if ( slot[i] && slot[i] != stat )
		    {
			ccp col = slot[i] < 0 ? col_minus : slot[i] > 0 ? col_plus : col_misc;
			printf("%s%c%u%u",
			    col,
			    count ? ',' : stat < 0 ? '+' : '-',
			    i/4+1, i%4+1 );
			count++;
		    }

		while ( count++ < 3 )
		    fputs("   ",stdout);
	    }
	    else
	    {
		uint i, count = 0;
		for ( i = 0; i < MKW_N_TRACKS; i++ )
		    if ( slot[i] != stat )
		    {
			ccp col = slot[i] < 0 ? col_minus : slot[i] > 0 ? col_plus : col_misc;
			printf(" %s%c%u.%u",
				col,
				slot[i] < 0 ? '-' : slot[i] > 0 ? '+' : '?' ,
				i/4+1, i%4+1 );
			count++;
		    }
		    else if ( required_slot != 31 && ( i == 13 || i == 20 || i == 21 ))
		    {
			fputs("     ",stdout);
			count++;
		    }

		while ( count++ < 3 )
		    fputs("     ",stdout);
	    }
	    printf("%s : %s\n",col_reset,arg);
	}
	ResetSZS(&szs);
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command isarena			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_is_arena()
{
    SetPatchFileModeReadonly();

    if ( brief_count || verbose < 0 )
	print_header = false;

    if (print_header)
	printf("\n%sstatus   filename%s\n%s%.237s%s\n",
		colout->heading, colout->reset,
		colout->heading, ThinLine300_3, colout->reset );

    int count = 0;
    enumError max_err = ERR_OK;

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,true);

	ccp status = 0, color  = 0;

	if ( err == ERR_NOT_EXISTS )
	{
	    if (opt_ignore)
		err = ERR_OK;
	    else
	    {
		status = "!NO-FILE";
		color  = colout->bad;
	    }
	}
	else if (err)
	{
	    status = "!ERROR";
	    color  = colout->bad;
	}
	else
	{
	    FindSpecialFilesSZS(&szs,false);
	    if (!szs.course_kmp_data)
	    {
		status = "!NO-KMP";
		color  = colout->bad;
	    }
	    else
	    {
		kmp_t kmp;
		InitializeKMP(&kmp);
		err = ScanKMP(&kmp,false,szs.course_kmp_data,szs.course_kmp_size,0);
		if (err)
		{
		    status = "!KMP-ERR";
		    color  = colout->bad;
		}
		else
		{
		    const IsArena_t is_arena = IsArenaKMP(&kmp);
		    switch (is_arena)
		    {
			case ARENA_NONE:
			    err	   = ERR_DIFFER;
			    status = "-RACE";
			    color  = colout->b_orange;
			    break;

			case ARENA_MAYBE:
			    err	   = ERR_DIFFER;
			    status = "?MAYBE";
			    color  = colout->b_yellow;
			    break;

			case ARENA_FOUND:
			    status = "+ARENA";
			    color  = colout->b_green;
			    break;

			case ARENA_DISPATCH:
			    status = "+DISPATCH";
			    color  = colout->b_green;
			    break;

			case ARENA__N:
			    break;
		    }

		    if (!status)
		    {
			status = "!UNKNOWN";
			color  = colout->bad;
		    }
		}
		ResetKMP(&kmp);
	    }
	}

	if (status)
	{
	    count++;
	    if ( verbose >= 0 )
	    {
		DASSERT(color);
		if (brief_count)
		    printf("%s%s%s\n", color, status, colout->reset );
		else
		    printf("%s%-9s%s %s\n", color, status, colout->reset, arg );
	    }
	}

	ResetSZS(&szs);
	if ( max_err < err )
	    max_err = err;
    }

    if (print_header)
	putchar('\n');

    ResetStringField(&plist);
    return count ? max_err : ERR_NOTHING_TO_DO;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command patch			///////////////
///////////////////////////////////////////////////////////////////////////////

static bool ScanByExtFF
	( file_format_t *ff_arch, file_format_t *ff_compr, ccp fname )
{
    DASSERT(ff_arch);
    DASSERT(ff_compr);
    *ff_arch  = FF_UNKNOWN;
    *ff_compr = FF_UNKNOWN;

    ccp point = strrchr(fname,'.');
    if (!point)
	return false;

    ++point;
    if (!strcmp(point,"szs"))
    {
	*ff_arch  = FF_U8;
	*ff_compr = FF_YAZ0;
	return true;
    }

    if (!strcmp(point,"wbz"))
    {
	*ff_arch  = FF_WU8;
	*ff_compr = FF_BZ;
	return true;
    }

    if ( !strcmp(point,"bz2") || !strcmp(point,"bzip2") )
    {
	*ff_arch  = FF_U8;
	*ff_compr = FF_BZIP2;
	return true;
    }

    if (!strcmp(point,"wlz"))
    {
	*ff_arch  = FF_WU8;
	*ff_compr = FF_LZ;
	return true;
    }

    if (!strcmp(point,"lzma"))
    {
	*ff_arch  = FF_U8;
	*ff_compr = FF_LZMA;
	return true;
    }

 #if 0 // [[xz+]]
    if (!strcmp(point,"xz"))
    {
	*ff_arch  = FF_U8;
	*ff_compr = FF_XZ;
	return true;
    }
 #endif

    if (!strcmp(point,"u8"))
    {
	*ff_arch = FF_U8;
	return true;
    }

    if (!strcmp(point,"wu8"))
    {
	*ff_arch = FF_WU8;
	return true;
    }

    if (!strcmp(point,"xyz"))
    {
	*ff_arch = FF_XYZ;
	return true;
    }

    if (!strcmp(point,"lta"))
    {
	*ff_arch = FF_LTA;
	return true;
    }

    if (!strcmp(point,"lfl"))
    {
	*ff_arch = FF_LFL;
	return true;
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////

// old [[container]] will be replaced by Container_t & ContainerData_t
#define PRINT_DATA PRINT("##### %s() #%u: cd=%p,%zu,%d / d=%p,%zu,%d / c=%p,%d #####\n", \
	__FUNCTION__, __LINE__, \
	szs.cdata, szs.csize, szs.cdata_alloced, \
	szs.data, szs.size, szs.data_alloced, \
	szs.old_container ? szs.old_container->data : 0, \
	szs.old_container ? szs.old_container->ref_count : -1 )

//-----------------------------------------------------------------------------

struct cmd_patch_search_t
{
    int			count;
    enumError		max_err;
};

//-----------------------------------------------------------------------------

static enumError patch_file_helper
(
    ccp		fname			// file name of source
)
{
    PRINT0("patch_file_helper(%s)\n",fname);

    exmem_t orig = {0}, data = {0};

    szs_file_t szs;
    InitializeSZS(&szs);
    enumError err = LoadSZS(&szs,fname,false,opt_ignore>0,true);

    if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
    {
	const bool was_compressed = szs.cdata != 0;
	if (was_compressed)
	{
	    DecompressSZS(&szs,false,0);
	    DASSERT(szs.cdata);
	    DASSERT(szs.cdata_alloced);
	    orig = ExMemMove(szs.cdata,szs.csize);
	    szs.cdata = 0;
	    szs.cdata_alloced = false;
	    data = ExMemDup(szs.data,szs.size);
	}
	else
	{
	    DASSERT(szs.data);
	    data = ExMemDup(szs.data,szs.size);
	 #if 1
	    orig = data;
	    orig.is_alloced = false;
	 #else
	    orig = ExMemDup(szs.data,szs.size);
	 #endif
	}

	const ccp src_format = GetNameFF_SZS(&szs);
	const bool convert_wu8 = opt_fform == FF_WU8
		    || opt_fform == FF_UNKNOWN && szs.fform_arch == FF_WU8;
	PRINT("CONVERT_WU8=%d, FF=%s\n", convert_wu8, src_format );

	DecodeWU8(&szs);

	//szs.allow_ext_data = opt_norm;
	szs.allow_ext_data = true;

	bool dirty = PatchSZS(&szs);
	//xBINGO;
	PRINT0("dirty=%d, opt_norm=%d, need_norm=%d, opt_auto_add=%d, opt_cup_icons=%s\n",
		dirty, opt_norm, need_norm, opt_auto_add, opt_cup_icons );

	if (( opt_norm || need_norm || opt_auto_add || opt_cup_icons || dirty ) && NormalizeSZS(&szs) )
	{
	    dirty = true;
	}
	if ( OptionUsed[OPT_MINIMAP] && AutoAdjustMinimap(&szs) )
	    dirty = true;

	if (convert_wu8)
	{
	    err = EncodeWU8(&szs);
	    if (err)
		goto abort;
	}


	//--- dest file

	char dest[PATH_MAX];
	if ( opt_fform == FF_U8 || opt_fform == FF_WU8 )
	{
	    SubstDest( dest, sizeof(dest), fname,
		opt_dest && *opt_dest ? opt_dest : "\1P/\1N\1?T", "\1N\1?T",
		GetExtFF(szs.fform_file,opt_fform), false );
	}
	else if ( opt_dest && *opt_dest )
	{
	    SubstDest( dest, sizeof(dest), fname, opt_dest, 0,
		GetExtFF(szs.fform_file,szs.fform_arch), false );
	}
	else
	    StringCopyS(dest,sizeof(dest),fname);


	//--- check dirty

	file_format_t ff_file = was_compressed ? GetNewCompressionSZS(&szs) : szs.fform_file;
	const ccp dest_format = GetNameFF(ff_file,szs.fform_current);
	const bool src_dest_diff
	    = strcmp(src_format,dest_format) || strcmp(dest,fname);

	if (!dirty)
	    dirty = data.data.len != szs.size || memcmp(data.data.ptr,szs.data,szs.size);

	if ( !dirty && was_compressed )
	{
 	    CompressSZS(&szs,0,true);
	    dirty = orig.data.len != szs.csize || memcmp(orig.data.ptr,szs.cdata,szs.csize);
	    FreeExMem(&orig);
	    orig = ExMemByS(szs.cdata,szs.csize);
	}

	if ( !dirty && ( !src_dest_diff || opt_no_copy ) )
	    goto abort;


	//--- compression and SZS cache

	if ( dirty || src_dest_diff )
	{
	    if (was_compressed)
	    {
		CompressSZS(&szs,0,true);
		FreeExMem(&orig);
		orig = ExMemByS(szs.cdata,szs.csize);
	    }
	    else
	    {
		FreeExMem(&orig);
		orig = ExMemByS(szs.data,szs.size);
	    }
	}


	if ( dirty || src_dest_diff )
	{
	    if ( verbose >= 0 || testmode )
	    {
		if (src_dest_diff)
		    fprintf(stdlog,"%s%s%s %s:%s -> %s:%s\n",
			    testmode ? "WOULD " : "",
			    opt_norm ? "NORMALIZE" : dirty ? "PATCH" : "COPY ",
			    szs.aiparam_removed ? " [-AIParam]" : "",
			    src_format, fname, dest_format, dest );
		else
		    fprintf(stdlog,"%s%s%s %s:%s\n",
			    testmode ? "WOULD " : "",
			    opt_norm ? "NORMALIZE" : "PATCH",
			    szs.aiparam_removed ? " [-AIParam]" : "",
			    dest_format, dest );
		fflush(stdlog);
	    }

	    File_t F;
	    err = CreateFileOpt(&F,true,dest,testmode,fname);
	    if (F.f)
	    {
		SetFileAttrib(&F.fatt,&szs.fatt,0);
		size_t wstat = fwrite(orig.data.ptr,1,orig.data.len,F.f);
		if ( wstat != orig.data.len )
		    err = FILEERROR1(&F,
			    ERR_WRITE_FAILED,
			    "Writing %u bytes failed [%zu bytes written]: %s\n",
			    orig.data.len, wstat, dest );
	    }
	    ResetFile(&F,opt_preserve);
	    if ( !err && opt_remove_src )
		RemoveSource(fname,dest,verbose>=0,testmode);
	}
	else if ( verbose > 0 )
	{
	    fprintf(stdlog,"ALREADY NORMALIZED: %s:%s\n", src_format, fname );
	    fflush(stdlog);
	}

	LinkCacheData(szs.cache_fname,dest,orig.data.ptr,orig.data.len);
    }

  abort:;
    FreeExMem(&orig);
    FreeExMem(&data);
    ResetSZS(&szs);
    return parallel_count > 0 && err == ERR_NOTHING_TO_DO ? ERR_OK : err;
}

//-----------------------------------------------------------------------------

static enumError cmd_patch()
{
 #if HAVE_PRINT
    file_format_t dest_ff_arch;
    file_format_t dest_ff_compr;
    const bool dest_ff_valid = ScanByExtFF(&dest_ff_arch,&dest_ff_compr,opt_dest);
    PRINT("fform(dest) = %s [valid=%d]\n",
		GetNameFF(dest_ff_compr,dest_ff_arch), dest_ff_valid );
 #endif

    enumError max_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];
	const enumError err = patch_file_helper(arg);
	if ( max_err < err )
	     max_err = err;
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command normalize		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_normalize()
{
    RegisterOptionByIndex(&InfoUI_wszst,OPT_NORM,1,false);
    opt_norm = true;
    return cmd_patch();
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command copy			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_copy()
{
    RegisterOptionByIndex(&InfoUI_wszst,OPT_OVERWRITE,1,false);
    opt_overwrite = true;

    if (!opt_dest)
    {
	if (!first_param)
	    return ERROR0(ERR_MISSING_PARAM, "Missing destination parameter!\n" );

	ParamList_t *param;
	for ( param = first_param; param->next; param = param->next )
	    ;
	ASSERT(param);
	ASSERT(!param->next);
	SetDest(param->arg,false);
	param->arg = 0;
    }

    return cmd_patch();
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command duplicate		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_duplicate()
{
    RegisterOptionByIndex(&InfoUI_wszst,OPT_OVERWRITE,1,false);
    opt_overwrite = true;


    //--- check parameters

    if (!first_param)
	return ERROR0(ERR_MISSING_PARAM, "Missing source file!\n" );

    if (!opt_dest)
    {
	if (!first_param->next)
	    return ERROR0(ERR_MISSING_PARAM, "Missing destination parameter!\n" );

	SetDest(first_param->next->arg,false);
	n_param--;
    }

    if ( n_param != 1 )
	return ERROR0(ERR_SEMANTIC,"Exact 1 source file expected!\n" );

    NORMALIZE_FILENAME_PARAM(first_param);
    ccp suffix, dest1 = opt_dest, dest2;
    {
	ccp slash = strrchr(dest1,'/');
	if (!slash)
	    slash = dest1;

	char *pos = strchr(slash,'@');
	if (pos)
	{
	    *pos = 0;
	    dest2 = pos+1;
	    while ( *dest2 == ' ' )
		dest2++;
	    suffix = !*dest2 || *dest2 == '.' ? "" : " ";
	    while ( pos > dest1 && pos[-1] == ' ' )
		*--pos = 0;
	}
	else
	{
	    pos = strrchr(slash,'.');
	    if (pos)
	    {
		*pos = 0;
		dest2 = pos+1;
		suffix = ".";
	    }
	    else
		dest2 = suffix = EmptyString;
	}
    }


    //--- open source

    szs_file_t szs;
    InitializeSZS(&szs);
    enumError err = LoadSZS(&szs,first_param->arg,false,opt_ignore>0,true);
    if (err)
	return err;

    const bool was_compressed = szs.cdata != 0;
    if (was_compressed)
	DecompressSZS(&szs,false,0);

    const ccp fform = GetNameFF_SZS(&szs);
    if ( verbose >= 0 )
    {
	fprintf(stdlog,"READ %s:%s\n",fform,first_param->arg);
	fflush(stdlog);
    }

    if ( szs.fform_current != FF_U8 && szs.fform_current != FF_WU8  )
// [[lta]] [[2do]]
// [[lfl]] [[2do]]
	return ERROR0(ERR_INVALID_DATA,"A track file (SZS or WBZ) expected!\n");

    force_lex_test = true; // force patch to insert LEX/TEST
    PatchSZS(&szs);


    //--- find KMP

    FindSpecialFilesSZS(&szs,false);
    if (!szs.course_kmp_data)
	return ERROR0(ERR_INVALID_DATA,"KMP not found → abort: %s\n",first_param->arg);

    kmp_t kmp;
    err = ScanKMP(&kmp,true,szs.course_kmp_data,szs.course_kmp_size,0);


    //--- find LEX/TEST

    if (!szs.course_lex_data)
	return ERROR0(ERR_INTERNAL,0);

    lex_element_t *lex_test = FindLexElement( (lex_header_t*)szs.course_lex_data,
						szs.course_lex_size, LEXS_TEST );
    if (!lex_test)
	return ERROR0(ERR_INTERNAL,0);

    kmp_ana_pflag_t ap;
    AnalysePFlagScenarios(&ap,&kmp,opt_gamemodes);

    char buf[10];
    const int fw = snprintf(buf,sizeof(buf),"%u",ap.n_res);

    uint ri;
    const kmp_ana_pflag_res_t *res;
    for ( ri = 0, res = ap.res_list; ri < ap.n_res; ri++, res++ )
    {
	memcpy(lex_test->data,&res->lex_test,sizeof(res->lex_test));
	char fname[PATH_MAX];
	snprintf(fname,sizeof(fname),"%s {%02u,%s}%s%s",
		dest1, res->version+1, res->name, suffix, dest2 );
	if ( verbose >= 0 || testmode > 0 )
	{
	    fprintf(stdlog,"%sCREATE %*u/%u %s:%s\n",
		testmode > 0 ? "WOULD " : "", fw, ri+1, ap.n_res, fform, fname );
	    fflush(stdlog);
	}

	if (was_compressed)
	{
	    ClearCompressedSZS(&szs);
	    CompressSZS(&szs,0,false);
	}

	SaveSZS(&szs,fname,true,was_compressed);
    }

    ResetKMP(&kmp);
    ResetSZS(&szs);
    return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command minimap			///////////////
///////////////////////////////////////////////////////////////////////////////

static void print_mmap ( ccp title, float3 *a, float3 *b )
{
    DASSERT(title);
    DASSERT(a);
    DASSERT(b);

    printf("%-13s %9.1f %9.1f %9.1f  %20.1f %9.1f %9.1f\n",
	title, a->x, a->y, a->z, b->x, b->y, b->z );
}

///////////////////////////////////////////////////////////////////////////////

static void print_matrix ( ccp title, float *a, float *b )
{
    DASSERT(title);
    DASSERT(a);
    DASSERT(b);

    uint i;
    for ( i = 0; i < 3; i++ )
    {
	printf("%s-Matrix-%c: %9.1f %9.1f %9.1f %9.1f   %9.1f %9.1f %9.1f %9.1f\n",
		title, 'X'+i,
		a[0], a[1], a[2], a[3],
		b[0], b[1], b[2], b[3] );
	a += 4;
	b += 4;
    }
}

///////////////////////////////////////////////////////////////////////////////

static void print_data
(
    mdl_minimap_t	*mmap,		// valid minimap record
    mdl_sect1_t		*root,		// NULL or valid 'root' data
    mdl_sect1_t		*ld,		// valid 'posLD' data
    mdl_sect1_t		*ru,		// valid 'posRU' data
    uint		mode,		// general print mode
    uint		modified	// 0:none, 1:translate, 2:all, 3:all+minimum
)
{
    if ( modified != 1 )
    {
	printf("Idx+Id+Flags: %9u %9u %#9x %21u %9u %#9x\n",
		ld->index, ld->id, ld->flags,
		ru->index, ru->id, ru->flags );
	print_mmap( "Scale:",	&ld->scale,  &ru->scale  );
	print_mmap( "Rotation",	&ld->rotate, &ru->rotate );
    }

    print_mmap( "Translation:",	&ld->translate, &ru->translate );
    if ( mmap->rec_trans_valid && ( !modified || mode > 0 ) )
	print_mmap(" Recommend.:",&mmap->rec_trans_ld,&mmap->rec_trans_ru);

    if ( mode > 1 && !modified )
    {
	print_mmap( "Minimum:", &ld->minimum, &ru->minimum );
	print_mmap( "Maximum:", &ld->maximum, &ru->maximum );
    }

    if ( mode > 0 )
    {
	putchar('\n');
	print_matrix("Tra",ld->trans_matrix.v,ru->trans_matrix.v);
	putchar('\n');
	print_matrix("Inv",ld->inv_matrix.v,ru->inv_matrix.v);
    }
    printf("%.95s\n",Minus300);

    if ( mode > 2 && !modified || modified > 2 )
    {
	printf("Vertex-Min: %11.1f %9.1f %9.1f\n",
			mmap->min.x, mmap->min.y, mmap->min.z );
	printf("Vertex-Max: %11.1f %9.1f %9.1f\n",
			mmap->max.x, mmap->max.y, mmap->max.z );
	if (mmap->root)
	{
	    printf("Root-Min:   %11.1f %9.1f %9.1f\n",
			root->minimum.x, root->minimum.y, root->minimum.z );
	    printf("Root-Max:   %11.1f %9.1f %9.1f\n",
			root->maximum.x, root->maximum.y, root->maximum.z );
	}
	printf("%.95s\n",Minus300);
    }
}

///////////////////////////////////////////////////////////////////////////////

static void center_mmap ( uint idx, float3 *a, float3 *b )
{
    DASSERT(idx<3);
    DASSERT(a);
    DASSERT(b);

    const float new_val = ( a->v[idx] - b->v[idx] ) / 2;
    a->v[idx] = +new_val;
    b->v[idx] = -new_val;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_minimap()
{
    enumError max_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadSZS(&szs,arg,false,opt_ignore>0,false);

	if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
	{
	    if ( verbose >= 0 )
		fprintf(stdlog,"\nMinimap data of %s:%s\n",
			GetNameFF_SZS(&szs), arg );

	    const bool was_compressed = szs.cdata != 0;
	    if (was_compressed)
		DecompressSZS(&szs,true,0);

	    bool dirty = false;
	    mdl_minimap_t mmap;
	    if (FindMinimapData(&mmap,&szs))
	    {
		DASSERT(mmap.posLD);
		DASSERT(mmap.posRU);
		PRINT("MMAP FOUND!\n");

		mdl_sect1_t root, ld, ru;
		ntoh_MDLs1(&root,mmap.root);
		ntoh_MDLs1(&ld,mmap.posLD);
		ntoh_MDLs1(&ru,mmap.posRU);

		if ( verbose >= 0 )
		{
		    printf("\n%53s%42s\n%.95s\n",
			"______________left-down_______________",
			"_______________right-up_______________",
			Minus300);
		    print_data(&mmap,&root,&ld,&ru,long_count,0);
		}

		if ( transform_active
			|| have_set_scale
			|| have_set_rot
			|| have_set_value
			|| OptionUsed[OPT_TOUCH]
			|| OptionUsed[OPT_AUTO]
			|| OptionUsed[OPT_XCENTER]
			|| OptionUsed[OPT_YCENTER]
			|| OptionUsed[OPT_ZCENTER]
			|| OptionUsed[OPT_CENTER]
			|| set_flags != M1(set_flags)
		   )
		{
		    uint modified = 1;
		    if ( OptionUsed[OPT_AUTO] && mmap.rec_trans_valid )
		    {
			modified = 2;
			ClearMDLs1(&ld,false);
			ld.translate = mmap.rec_trans_ld;
			ClearMDLs1(&ru,false);
			ru.translate = mmap.rec_trans_ru;
		    }

		    if ( set_flags != M1(set_flags) )
		    {
			modified = 2;
			ld.flags = ru.flags = set_flags;
		    }

		    if (have_set_scale)
		    {
			modified = 2;
			ld.scale.x = ru.scale.x = set_scale.x;
			ld.scale.y = ru.scale.y = set_scale.y;
			ld.scale.z = ru.scale.z = set_scale.z;
		    }

		    if (have_set_rot)
		    {
			modified = 2;
			ld.rotate.x = ru.rotate.x = set_rot.x;
			ld.rotate.y = ru.rotate.y = set_rot.y;
			ld.rotate.z = ru.rotate.z = set_rot.z;
		    }

		    if ( have_set_value & 1 )
		    {
			ld.translate.x = set_value_min.x;
			ru.translate.x = set_value_max.x;
		    }
		    if ( have_set_value & 2 )
		    {
			ld.translate.y = set_value_min.y;
			ru.translate.y = set_value_max.y;
		    }
		    if ( have_set_value & 4 )
		    {
			ld.translate.z = set_value_max.z;
			ru.translate.z = set_value_min.z;
		    }

		    if ( OptionUsed[OPT_XCENTER] || OptionUsed[OPT_CENTER] )
			center_mmap(0,&ld.translate,&ru.translate);
		    if ( OptionUsed[OPT_YCENTER] || OptionUsed[OPT_CENTER] )
			center_mmap(1,&ld.translate,&ru.translate);
		    if ( OptionUsed[OPT_ZCENTER] || OptionUsed[OPT_CENTER] )
			center_mmap(2,&ld.translate,&ru.translate);

		    TransformPosFloat3D(ld.translate.v,1,0);
		    TransformPosFloat3D(ru.translate.v,1,0);
		    CalcMatrixMDLs1(&ld);
		    CalcMatrixMDLs1(&ru);

		    hton_MDLs1(&ld,&ld);
		    hton_MDLs1(&ru,&ru);
		    dirty = memcmp(mmap.posLD,&ld,sizeof(ld))
			 || memcmp(mmap.posRU,&ru,sizeof(ru));

#if 1
		    if (mmap.root)
		    {
			root.minimum = mmap.min;
			root.maximum = mmap.max;

	    #if 0 // workaround fails
			// workaround for a bug in KMP modifier
			if ( root.minimum.x > root.minimum.z )
			     root.minimum.x = root.minimum.z;
			if ( root.maximum.x < root.maximum.z )
			     root.maximum.x = root.maximum.z;
	    #endif

			hton_MDLs1(&root,&root);
			if (memcmp(mmap.root,&root,sizeof(root)))
			{
			    memcpy(mmap.root,&root,sizeof(root));
			    modified = 3;
			    dirty = true;
			}
			ntoh_MDLs1(&root,&root);
		    }
#else
		    if ( mmap.root
			&& (   memcmp(&root.minimum,&mmap.min,sizeof(root.minimum))
			    || memcmp(&root.maximum,&mmap.max,sizeof(root.maximum)) ))
		    {
			root.minimum = mmap.min;
			root.maximum = mmap.max;

			hton_MDLs1(mmap.root,&root);
			modified = 3;
			dirty = true;
		    }
#endif

		    if (dirty)
		    {
			memcpy(mmap.posLD,&ld,sizeof(ld));
			memcpy(mmap.posRU,&ru,sizeof(ru));
			ntoh_MDLs1(&ld,&ld);
			ntoh_MDLs1(&ru,&ru);
			if ( verbose >= 0 )
			    print_data(&mmap,&root,&ld,&ru,long_count,modified);
		    }
		}
	    }

	    if (dirty)
	    {
		if ( opt_norm || need_norm > 0 )
		    NormalizeSZS(&szs);

		if ( verbose >= 0 || testmode )
		{
		    fprintf(stdlog,"%sPATCH MINIMAP %s:%s\n",
			testmode ? "WOULD " : "",
			GetNameFF_SZS(&szs), arg );
		    fflush(stdlog);
		}

		if (!testmode)
		    SaveSZS(&szs,szs.fname,true,was_compressed);
	    }
	}
	fflush(stdlog);

	if ( max_err < err )
	     max_err = err;
	ResetSZS(&szs);
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command compress		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError EncodeZlib ( u8 **dest, uint *dest_size, const u8 *src, size_t src_size, bool raw_deflate )
{
    if (!dest || !dest_size || !src) return ERR_SEMANTIC;
    *dest = 0; *dest_size = 0;
    uLongf bound = compressBound((uLong)src_size) + 64;
    u8 *buf = MALLOC(bound);
    if (!buf) return ERR_OUT_OF_MEMORY;

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    int windowBits = raw_deflate ? -15 : 15;
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        FREE(buf);
        return ERR_INVALID_DATA;
    }
    strm.next_in = (Bytef*)src;
    strm.avail_in = (uInt)src_size;
    strm.next_out = buf;
    strm.avail_out = (uInt)bound;
    int ret = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);
    if (ret != Z_STREAM_END)
    {
        FREE(buf);
        return ERR_INVALID_DATA;
    }
    *dest = buf;
    *dest_size = (uint)strm.total_out;
    return ERR_OK;
}

static enumError DecodeZlib ( u8 **dest, uint *dest_size, const u8 *src, size_t src_size, bool raw_deflate )
{
    if (!dest || !dest_size || !src) return ERR_SEMANTIC;
    *dest = 0; *dest_size = 0;
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    int windowBits = raw_deflate ? -15 : 15;
    if (inflateInit2(&strm, windowBits) != Z_OK)
        return ERR_INVALID_DATA;

    size_t out_cap = src_size * 4 + 1024;
    u8 *buf = MALLOC(out_cap);
    if (!buf) { inflateEnd(&strm); return ERR_OUT_OF_MEMORY; }

    strm.next_in = (Bytef*)src;
    strm.avail_in = (uInt)src_size;
    strm.next_out = buf;
    strm.avail_out = (uInt)out_cap;

    while (1)
    {
        int ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_END) break;
        if (ret != Z_OK && ret != Z_BUF_ERROR)
        {
            FREE(buf);
            inflateEnd(&strm);
            return ERR_INVALID_DATA;
        }
        if (strm.avail_out == 0)
        {
            size_t new_cap = out_cap * 2;
            u8 *new_buf = REALLOC(buf, new_cap);
            if (!new_buf) { FREE(buf); inflateEnd(&strm); return ERR_OUT_OF_MEMORY; }
            buf = new_buf;
            strm.next_out = buf + out_cap;
            strm.avail_out = (uInt)(new_cap - out_cap);
            out_cap = new_cap;
        }
    }
    *dest_size = (uint)strm.total_out;
    inflateEnd(&strm);
    *dest = buf;
    return ERR_OK;
}

static enumError compress_nintendo_file ( ccp arg )
{
    if (!opt_dest) return ERR_NOTHING_TO_DO;
    ccp ext = strrchr(opt_dest,'.');
    if (ext && !strcasecmp(ext, ".wux"))
    {
	char dest[PATH_MAX];
	SubstDest(dest,sizeof(dest),arg,opt_dest,ext,ext,false);
	if (verbose >= 0 || testmode)
	    fprintf(stdlog,"%s%sCOMPRESS WUX:%s -> RAW:%s\n",
		verbose > 0 ? "\n" : "",testmode ? "WOULD " : "",arg,dest);
	if (testmode) return ERR_OK;
	return wux_compress(arg, dest) ? ERR_OK : ERR_WRITE_FAILED;
    }
    if (!ext || (strcasecmp(ext,".lz10") && strcasecmp(ext,".lz11") && strcasecmp(ext,".rl") && strcasecmp(ext,".yay0")
	&& strcasecmp(ext,".ash") && strcasecmp(ext,".ash0") && strcasecmp(ext,".lzh8") && strcasecmp(ext,".qlz")
	&& strcasecmp(ext,".at7") && strcasecmp(ext,".at7p") && strcasecmp(ext,".blz")
	&& strcasecmp(ext,".huff4") && strcasecmp(ext,".huff8") && strcasecmp(ext,".huff")
	&& strcasecmp(ext,".stpl") && strcasecmp(ext,".camelot")
	&& strcasecmp(ext,".rnc") && strcasecmp(ext,".rnc1") && strcasecmp(ext,".rnc2")
	&& strcasecmp(ext,".zlib") && strcasecmp(ext,".deflate")))
        return ERR_NOTHING_TO_DO;
    u8 *data = 0, *packed = 0;
    size_t file_size = 0;
    uint packed_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&data,&file_size,0,0,0,false);
    if (!err && file_size > UINT_MAX) err = ERR_FILE_TOO_BIG;
    if (!err)
	err = !strcasecmp(ext,".rl")
	    ? EncodeNintendoRL(&packed,&packed_size,data,file_size)
	    : !strcasecmp(ext,".ash") || !strcasecmp(ext,".ash0")
	    ? EncodeASH0(&packed,&packed_size,data,file_size)
	    : !strcasecmp(ext,".yay0")
	    ? EncodeYay0(&packed,&packed_size,data,file_size)
	    : !strcasecmp(ext,".lzh8")
	    ? EncodeLZH8(&packed,&packed_size,data,file_size)
	    : !strcasecmp(ext,".qlz")
	    ? EncodeQuickLZ(&packed,&packed_size,data,file_size)
	    : !strcasecmp(ext,".at7") || !strcasecmp(ext,".at7p")
	    ? EncodeAT7(&packed,&packed_size,data,file_size)
	    : !strcasecmp(ext,".blz")
	    ? EncodeBLZ(&packed,&packed_size,data,file_size)
	    : !strcasecmp(ext,".huff4")
	    ? EncodeNintendoHuff(&packed,&packed_size,data,file_size,true)
	    : !strcasecmp(ext,".huff8") || !strcasecmp(ext,".huff")
	    ? EncodeNintendoHuff(&packed,&packed_size,data,file_size,false)
	    : !strcasecmp(ext,".stpl") || !strcasecmp(ext,".camelot")
	    ? EncodeCamelot(&packed,&packed_size,data,file_size)
	    : !strcasecmp(ext,".rnc") || !strcasecmp(ext,".rnc1") || !strcasecmp(ext,".rnc2")
	    ? EncodeRNC(&packed,&packed_size,data,file_size,2)
	    : !strcasecmp(ext,".zlib")
	    ? EncodeZlib(&packed,&packed_size,data,file_size,false)
	    : !strcasecmp(ext,".deflate")
	    ? EncodeZlib(&packed,&packed_size,data,file_size,true)
	    : EncodeLZ10LZ11(&packed,&packed_size,data,file_size,!strcasecmp(ext,".lz11"));
    FREE(data);
    if (err) { FREE(packed); return err; }
    char dest[PATH_MAX];
    SubstDest(dest,sizeof(dest),arg,opt_dest,ext,ext,false);
    if (verbose >= 0 || testmode)
        fprintf(stdlog,"%s%sCOMPRESS %s:%s -> RAW:%s\n",
            verbose > 0 ? "\n" : "",testmode ? "WOULD " : "",
            !strcasecmp(ext,".rl") ? "RL" : !strcasecmp(ext,".ash") || !strcasecmp(ext,".ash0") ? "ASH0"
		: !strcasecmp(ext,".yay0") ? "Yay0" : !strcasecmp(ext,".lzh8") ? "LZH8"
		: !strcasecmp(ext,".qlz") ? "QuickLZ"
		: !strcasecmp(ext,".at7") || !strcasecmp(ext,".at7p") ? "AT7"
		: !strcasecmp(ext,".blz") ? "BLZ"
		: !strcasecmp(ext,".huff4") ? "Huffman4"
		: !strcasecmp(ext,".huff8") || !strcasecmp(ext,".huff") ? "Huffman8"
		: !strcasecmp(ext,".stpl") || !strcasecmp(ext,".camelot") ? "Camelot"
		: !strcasecmp(ext,".rnc") || !strcasecmp(ext,".rnc1") || !strcasecmp(ext,".rnc2") ? "RNC"
		: !strcasecmp(ext,".zlib") ? "Zlib"
		: !strcasecmp(ext,".deflate") ? "Deflate"
		: !strcasecmp(ext,".lz11") ? "LZ11" : "LZ10",arg,dest);
    if (!testmode)
    {
	File_t F;
	err = CreateFileOpt(&F,true,dest,false,arg);
	if (F.f && fwrite(packed,1,packed_size,F.f) != packed_size)
	    err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing %u bytes failed: %s\n",packed_size,dest);
	ResetFile(&F,opt_preserve);
    }
    FREE(packed);
    return err;
}

static enumError cmd_compress()
{
    static const char dest_fname[] = "\1P/\1N\1?T";
    CheckOptDest(dest_fname,false);

    enumError max_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];
	enumError native_err = compress_nintendo_file(arg);
	if (native_err != ERR_NOTHING_TO_DO)
	{
	    if (max_err < native_err) max_err = native_err;
	    continue;
	}

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadSZS(&szs,arg,true,opt_ignore>0,false);

	if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
	{
	    file_format_t ff_dest = szs.fform_arch;
	    if (   ff_dest == FF_U8  && opt_fform == FF_WU8
		|| ff_dest == FF_WU8 && opt_fform == FF_U8 )
	    {
		 ff_dest = opt_fform;
	    }
	    PRINT("FF = %s , %s => %s\n",
		    GetNameFF_SZS(&szs),
		    GetNameFF_SZScurrent(&szs),
		    GetNameFF(0,ff_dest) );

	    char dest[PATH_MAX];
	    SubstDest(dest,sizeof(dest),arg,opt_dest,dest_fname,
			    GetExtFF(fform_compr,ff_dest),false);
	    if ( verbose >= 0 || testmode )
	    {
		fprintf(stdlog,"%s%sCOMPRESS %s:%s -> %s:%s\n",
			    verbose > 0 ? "\n" : "",
			    testmode ? "WOULD " : "",
			    GetNameFF_SZS(&szs), arg,
			    GetNameFF(fform_compr,ff_dest), dest );
		fflush(stdlog);
	    }

	    if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
	    {
		PatchSZS(&szs);
		if ( opt_norm || need_norm > 0 )
		    NormalizeSZS(&szs);

		if ( ff_dest == FF_WU8 )
		    err = EncodeWU8(&szs);

		if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
		{
		    szs.dest_fname = dest;
		    CompressSZS(&szs,0,true);
		    szs.dest_fname = 0;

		    if ( szs.cache_used && (int)max_err < ERR_CACHE_USED && parallel_count <= 0 )
			max_err = ERR_CACHE_USED;

		    File_t F;
		    err = CreateFileOpt(&F,true,dest,testmode,arg);
		    if (F.f)
		    {
			SetFileAttrib(&F.fatt,&szs.fatt,0);
			size_t wstat = fwrite(szs.cdata,1,szs.csize,F.f);
			if ( wstat != szs.csize )
			    err = FILEERROR1(&F,ERR_WRITE_FAILED,
					"Writing %zu bytes failed: %s\n",
					szs.csize, dest);
		    }
		    ResetFile(&F,opt_preserve);
		    LinkCacheData(szs.cache_fname,dest,szs.cdata,szs.csize);

		    if ( !err && opt_remove_src )
			RemoveSource(arg,dest,verbose>=0,testmode);
		}
	    }
	}

	if ( max_err < err )
	     max_err = err;
	ResetSZS(&szs);
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command decompress		///////////////
///////////////////////////////////////////////////////////////////////////////

// Decompress raw Nintendo codecs through the normal wszst destination and
// overwrite path. They are deliberately handled before LoadSZS(), because
// their payload is not an archive.
static enumError decompress_nintendo_file2 ( ccp arg, char *dest_out, uint dest_out_size );

static enumError decompress_nintendo_file ( ccp arg )
{
    return decompress_nintendo_file2(arg,0,0);
}

static enumError decompress_nintendo_file2 ( ccp arg, char *dest_out, uint dest_out_size )
{
    // Like extract_at7_file(), this has no extension gate (compression magic
    // can appear in any filename) so it runs against every file reaching
    // extract_one_file() -- including multi-gigabyte pass-through sources.
    // Cap the load and suppress LoadFileAlloc's own messages so an oversized
    // file is declined quietly instead of fully read into memory (see
    // extract_at7_file()'s comment for how this was found: the raw EFBIG
    // errno, 27, used to be returned here and aliased ERU_WARNING in this
    // file's own enumError enum, silently aborting the whole XX pipeline).
    u8 *data = 0, *decoded = 0;
    size_t file_size = 0;
    uint decoded_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&data,&file_size,UINT_MAX,2,0,false);
    if (err)
        return ERR_NOTHING_TO_DO;
    const uint size = file_size;

    // BLZ ("backward LZSS", DS ARM9/ARM7/overlay compression) has no magic
    // at the start to detect by -- everything is a footer at the end, and
    // any file could coincidentally have a plausible-looking one. So unlike
    // every other codec here it's dispatched by the SOURCE extension, not
    // DetectNintendoFormat()'s header-magic table; the pass-through side
    // (ndstool-staged arm9.bin/arm7.bin/overlay) calls DecodeBLZ() directly
    // instead, for the same reason.
    ccp src_ext = strrchr(arg,'.');
    if ( src_ext && !strcasecmp(src_ext,".blz") )
    {
	err = DecodeBLZ(&decoded,&decoded_size,data,size);
	FREE(data);
	if (err) return err;

	char dest[PATH_MAX];
	if (opt_dest)
	    SubstDest(dest,sizeof(dest),arg,opt_dest,0,".bin",false);
	else
	{
	    snprintf(dest,sizeof(dest),"%s",arg);
	    char *dot = strrchr(dest,'.');
	    if (dot) *dot = 0;
	    snprintf(dest+strlen(dest),sizeof(dest)-strlen(dest),".bin");
	}
	if (verbose >= 0 || testmode)
	    fprintf(stdlog,"%s%sDECOMPRESS BLZ:%s -> RAW:%s\n",
		verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
	if (!testmode)
	{
	    File_t F;
	    err = CreateFileOpt(&F,true,dest,false,arg);
	    if (F.f && fwrite(decoded,1,decoded_size,F.f) != decoded_size)
		err = FILEERROR1(&F,ERR_WRITE_FAILED,
		    "Writing %u bytes failed: %s\n",decoded_size,dest);
	    ResetFile(&F,opt_preserve);
	}
	FREE(decoded);
	if ( !err && dest_out )
	    snprintf(dest_out,dest_out_size,"%s",dest);
	return err;
    }

    if ( src_ext && ( !strcasecmp(src_ext,".zlib") || !strcasecmp(src_ext,".deflate") ) )
    {
	const bool raw_deflate = !strcasecmp(src_ext,".deflate");
	err = DecodeZlib(&decoded,&decoded_size,data,size,raw_deflate);
	FREE(data);
	if (err) return err;

	char dest[PATH_MAX];
	if (opt_dest)
	    SubstDest(dest,sizeof(dest),arg,opt_dest,0,".bin",false);
	else
	{
	    snprintf(dest,sizeof(dest),"%s",arg);
	    char *dot = strrchr(dest,'.');
	    if (dot) *dot = 0;
	    snprintf(dest+strlen(dest),sizeof(dest)-strlen(dest),".bin");
	}
	if (verbose >= 0 || testmode)
	    fprintf(stdlog,"%s%sDECOMPRESS %s:%s -> RAW:%s\n",
		verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
		raw_deflate ? "Deflate" : "Zlib", arg, dest);
	if (!testmode)
	{
	    File_t F;
	    err = CreateFileOpt(&F,true,dest,false,arg);
	    if (F.f && fwrite(decoded,1,decoded_size,F.f) != decoded_size)
		err = FILEERROR1(&F,ERR_WRITE_FAILED,
		    "Writing %u bytes failed: %s\n",decoded_size,dest);
	    ResetFile(&F,opt_preserve);
	}
	FREE(decoded);
	if ( !err && dest_out )
	    snprintf(dest_out,dest_out_size,"%s",dest);
	return err;
    }

    if ( src_ext && !strcasecmp(src_ext,".wux") )
    {
	FREE(data);
	char dest[PATH_MAX];
	if (opt_dest)
	    SubstDest(dest,sizeof(dest),arg,opt_dest,0,".wud",false);
	else
	{
	    snprintf(dest,sizeof(dest),"%s",arg);
	    char *dot = strrchr(dest,'.');
	    if (dot) *dot = 0;
	    snprintf(dest+strlen(dest),sizeof(dest)-strlen(dest),".wud");
	}
	if (verbose >= 0 || testmode)
	    fprintf(stdlog,"%s%sDECOMPRESS WUX:%s -> RAW:%s\n",
		verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
	if (!testmode)
	{
	    if (!wux_decompress(arg, dest))
		return ERR_WRITE_FAILED;
	}
	if ( dest_out )
	    snprintf(dest_out,dest_out_size,"%s",dest);
	return ERR_OK;
    }

    const nfmt_info_t info = DetectNintendoFormat(data,size,arg);
    switch (info.type)
    {
        case NFMT_LZ10:
        case NFMT_LZ11: err = DecodeLZ10LZ11(&decoded,&decoded_size,data,size); break;
        case NFMT_HUFF4:
        case NFMT_HUFF8: err = DecodeNintendoHuff(&decoded,&decoded_size,data,size); break;
        case NFMT_RL: err = DecodeNintendoRL(&decoded,&decoded_size,data,size); break;
        case NFMT_ASH0: err = DecodeASH0(&decoded,&decoded_size,data,size); break;
        case NFMT_YAY0: err = DecodeYay0(&decoded,&decoded_size,data,size); break;
        case NFMT_LZH8: err = DecodeLZH8(&decoded,&decoded_size,data,size); break;
        case NFMT_QLZ: err = DecodeQuickLZ(&decoded,&decoded_size,data,size); break;
        case NFMT_STPL: err = DecodeCamelot(&decoded,&decoded_size,data,size); break;
        case NFMT_RNC: err = DecodeRNC(&decoded,&decoded_size,data,size); break;
        case NFMT_AT7: err = DecodeAT7(&decoded,&decoded_size,data,size); break;

        // Recognized codecs with no in-tree decoder.  Report them clearly
        // instead of silently treating the payload as unknown data; a future
        // build can fill in PSDK decoding without touching this switch.
        case NFMT_PSDK:
        {
            const enumError e = ERROR0(ERR_WRONG_FILE_TYPE,
                "Codec %s is recognized but not yet supported "
                "(no decoder in this build): %s\n",
                GetNintendoFormatName(info.type),arg);
            FREE(data);
            return e;
        }

        default: FREE(data); return ERR_NOTHING_TO_DO;
    }
    FREE(data);
    if (err)
        return err;

    char dest[PATH_MAX];
    const ccp ext = info.type == NFMT_STPL ? ".tpl" : ".bin";
    if (opt_dest)
        SubstDest(dest,sizeof(dest),arg,opt_dest,0,ext,false);
    else
    {
        snprintf(dest,sizeof(dest),"%s",arg);
        char *dot = strrchr(dest,'.');
        if (dot) *dot = 0;
        snprintf(dest+strlen(dest),sizeof(dest)-strlen(dest),"%s",ext);
    }
    if (verbose >= 0 || testmode)
        fprintf(stdlog,"%s%sDECOMPRESS %s:%s -> RAW:%s\n",
            verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
            GetNintendoFormatName(info.type),arg,dest);

    if (!testmode)
    {
        File_t F;
        err = CreateFileOpt(&F,true,dest,false,arg);
        if (F.f && fwrite(decoded,1,decoded_size,F.f) != decoded_size)
            err = FILEERROR1(&F,ERR_WRITE_FAILED,
                "Writing %u bytes failed: %s\n",decoded_size,dest);
        ResetFile(&F,opt_preserve);
    }
    FREE(decoded);
    if ( !err && dest_out )
        snprintf(dest_out,dest_out_size,"%s",dest);
    return err;
}

static enumError cmd_decompress()
{
    static const char dest_fname[] = "\1P/\1N\1?T";
    CheckOptDest(dest_fname,false);

    enumError max_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];
	enumError native_err = decompress_nintendo_file(arg);
	if (native_err != ERR_NOTHING_TO_DO)
	{
	    if ( max_err < native_err )
		max_err = native_err;
	    continue;
	}

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadSZS(&szs,arg,true,opt_ignore>0,false);

	if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
	{
	    file_format_t ff_dest = szs.fform_arch;
	    if (   ff_dest == FF_U8  && opt_fform == FF_WU8
		|| ff_dest == FF_WU8 && opt_fform == FF_U8 )
	    {
		 ff_dest = opt_fform;
	    }
	    PRINT("FF = %s , %s => %s\n",
		    GetNameFF_SZS(&szs),
		    GetNameFF_SZScurrent(&szs),
		    GetNameFF(0,ff_dest) );

	    char dest[PATH_MAX];
	    ccp ext = GetExtFF(ff_dest,0);
	    SubstDest(dest,sizeof(dest),arg,opt_dest,ext,ext,false);

	    if ( verbose >= 0 || testmode )
	    {
		fprintf(stdlog,"%s%sDECOMPRESS %s:%s -> %s:%s\n",
			    verbose > 0 ? "\n" : "",
			    testmode ? "WOULD " : "",
			    GetNameFF_SZS(&szs), arg,
			    GetNameFF(0,ff_dest), dest );
		fflush(stdlog);
	    }

	    if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
	    {
		PatchSZS(&szs);
		if ( opt_norm || need_norm > 0 )
		    NormalizeSZS(&szs);

		if ( ff_dest == FF_WU8 )
		    err = EncodeWU8(&szs);

		if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
		{
		    File_t F;
		    err = CreateFileOpt(&F,true,dest,testmode,arg);
		    if (F.f)
		    {
			SetFileAttrib(&F.fatt,&szs.fatt,0);
			size_t wstat = fwrite(szs.data,1,szs.size,F.f);
			if ( wstat != szs.size )
			    err = FILEERROR1(&F,ERR_WRITE_FAILED,
					"Writing %zu bytes failed: %s\n",
					szs.size, dest);
		    }
		    ResetFile(&F,opt_preserve);
		    if ( !err && opt_remove_src )
			RemoveSource(arg,dest,verbose>=0,testmode);
		}
	    }
	}

	if ( max_err < err )
	     max_err = err;
	ResetSZS(&szs);
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command create			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct sarc_build_list_t
{
    nintendo_sarc_entry_t *entry;
    uint used, size;
}
sarc_build_list_t;

static void reset_sarc_build_list ( sarc_build_list_t *list )
{
    for (uint i = 0; i < list->used; i++)
    {
	FREE((void*)list->entry[i].name);
	FREE((void*)list->entry[i].data);
    }
    FREE(list->entry);
    memset(list,0,sizeof(*list));
}

static enumError collect_sarc_dir
(
    sarc_build_list_t *list, ccp root, ccp rel
)
{
    char path[PATH_MAX];
    snprintf(path,sizeof(path),"%s%s%s",root,*rel ? "/" : "",rel);
    DIR *dir = opendir(path);
    if (!dir) return ERROR0(ERR_NOT_EXISTS,"Can't open SARC input directory: %s\n",path);
    enumError err = ERR_OK;
    struct dirent *de;
    while (!err && (de = readdir(dir)))
    {
	if (!strcmp(de->d_name,".") || !strcmp(de->d_name,"..")) continue;
	char child_rel[PATH_MAX], child_path[PATH_MAX];
	snprintf(child_rel,sizeof(child_rel),"%s%s%s",rel,*rel ? "/" : "",de->d_name);
	snprintf(child_path,sizeof(child_path),"%s/%s",root,child_rel);
	struct stat st;
	if (lstat(child_path,&st)) { err = ERROR0(ERR_NOT_EXISTS,"Can't stat SARC input: %s\n",child_path); break; }
	if (S_ISDIR(st.st_mode))
	    err = collect_sarc_dir(list,root,child_rel);
	else if (S_ISREG(st.st_mode))
	{
	    if (list->used == list->size)
	    {
		const uint nsize = list->size ? 2*list->size : 32;
		void *ptr = REALLOC(list->entry,nsize*sizeof(*list->entry));
		if (!ptr) { err = ERR_CANT_CREATE; break; }
		list->entry = ptr;
		list->size = nsize;
	    }
	    u8 *data = 0;
	    size_t size = 0;
	    err = LoadFileAlloc(child_path,0,0,&data,&size,0,0,0,false);
	    if (err || size > UINT_MAX) { FREE(data); if (!err) err = ERR_FILE_TOO_BIG; if(err) ERROR0(err,"Can't load SARC input: %s\n",child_path); break; }
	    nintendo_sarc_entry_t *entry = list->entry + list->used++;
	    entry->name = STRDUP(child_rel);
	    entry->data = data;
	    entry->size = size;
	}
    }
    closedir(dir);
    return err;
}

// The conventional .sarc spelling keeps Nintendo's big-endian form.  The
// explicit suffixes make it possible to create the Wii U/Switch-style little
// endian variant without adding a global option whose meaning would leak into
// all existing archive formats.
static enumError create_sarc_dir ( ccp source, ccp dest, bool big_endian )
{
    sarc_build_list_t list = {0};
    enumError err = collect_sarc_dir(&list,source,"");
    if (!err && !list.used) err = ERR_NOTHING_TO_DO;
    u8 *data = 0;
    uint size = 0;
    if (!err) err = CreateSARC(&data,&size,list.entry,list.used,big_endian);
    if (!err && !testmode)
    {
	File_t F;
	err = CreateFileOpt(&F,true,dest,false,source);
	if (F.f && fwrite(data,1,size,F.f) != size)
	    err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing %u bytes failed: %s\n",size,dest);
	ResetFile(&F,opt_preserve);
    }
    FREE(data);
    reset_sarc_build_list(&list);
    return err;
}

static enumError create_narc_dir ( ccp source, ccp dest, bool is_le )
{
    sarc_build_list_t list = {0};
    enumError err = collect_sarc_dir(&list,source,"");
    if (!err && !list.used) err = ERR_NOTHING_TO_DO;
    u8 *data = 0;
    uint size = 0;
    if (!err) err = CreateNARC(&data,&size,list.entry,list.used,is_le);
    if (!err && !testmode)
    {
	File_t F;
	err = CreateFileOpt(&F,true,dest,false,source);
	if (F.f && fwrite(data,1,size,F.f) != size)
	    err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing %u bytes failed: %s\n",size,dest);
	ResetFile(&F,opt_preserve);
    }
    FREE(data);
    reset_sarc_build_list(&list);
    return err;
}

static enumError create_darc_dir ( ccp source, ccp dest )
{
    sarc_build_list_t list = {0};
    enumError err = collect_sarc_dir(&list,source,"");
    if (!err && !list.used) err = ERR_NOTHING_TO_DO;
    u8 *data = 0;
    uint size = 0;
    if (!err) err = CreateDARC(&data,&size,list.entry,list.used);
    if (!err && !testmode)
    {
	File_t F;
	err = CreateFileOpt(&F,true,dest,false,source);
	if (F.f && fwrite(data,1,size,F.f) != size)
	    err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing %u bytes failed: %s\n",size,dest);
	ResetFile(&F,opt_preserve);
    }
    FREE(data);
    reset_sarc_build_list(&list);
    return err;
}

static enumError create_pac_dir ( ccp source, ccp dest )
{
    sarc_build_list_t list = {0};
    enumError err = collect_sarc_dir(&list,source,"");
    if (!err && !list.used) err = ERR_NOTHING_TO_DO;
    u8 *data = 0;
    uint size = 0;
    if (!err) err = CreatePAC(&data,&size,list.entry,list.used);
    if (!err && !testmode)
    {
	File_t F;
	err = CreateFileOpt(&F,true,dest,false,source);
	if (F.f && fwrite(data,1,size,F.f) != size)
	    err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing %u bytes failed: %s\n",size,dest);
	ResetFile(&F,opt_preserve);
    }
    FREE(data);
    reset_sarc_build_list(&list);
    return err;
}

static enumError create_gfa_dir ( ccp source, ccp dest )
{
    sarc_build_list_t list = {0};
    enumError err = collect_sarc_dir(&list,source,"");
    if (!err && !list.used) err = ERR_NOTHING_TO_DO;
    u8 *data = 0;
    uint size = 0;
    if (!err) err = CreateGFA(&data,&size,list.entry,list.used);
    if (!err && !testmode)
    {
	File_t F;
	err = CreateFileOpt(&F,true,dest,false,source);
	if (F.f && fwrite(data,1,size,F.f) != size)
	    err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing %u bytes failed: %s\n",size,dest);
	ResetFile(&F,opt_preserve);
    }
    FREE(data);
    reset_sarc_build_list(&list);
    return err;
}

typedef struct ncer_xml_cell_t
{
    uint count, object_off;
}
ncer_xml_cell_t;

// The XML reader intentionally accepts the narrow, stable manifest emitted by
// extract_nitro_sprite_manifest().  It is not a general XML parser: rejecting
// extensions outside that vocabulary prevents a malformed project from being
// converted into an unsafe binary layout.
static enumError create_ncer_xml ( ccp source, ccp dest )
{
    u8 *xml = 0;
    size_t xml_size = 0;
    enumError err = LoadFileAlloc(source,0,0,&xml,&xml_size,16<<20,0,0,false);
    if (err) return err;
    uint n_cells = 0;
    if (sscanf((ccp)xml,"<?xml version=\"1.0\"?>\n<ncer cells=\"%u\">",&n_cells) != 1
        || !n_cells || n_cells > 65535)
        { FREE(xml); return ERR_INVALID_DATA; }
    ncer_xml_cell_t *cells = CALLOC(n_cells,sizeof(*cells));
    u16 *objects = 0;
    uint n_objects = 0, obj_alloc = 0;
    ccp p = (ccp)xml;
    for (uint i = 0; i < n_cells; i++)
    {
        p = strstr(p,"<cell ");
        uint index = 0, count = 0;
        if (!p || sscanf(p,"<cell index=\"%u\" objects=\"%u\">",&index,&count) != 2
            || index != i || count > 65535-n_objects)
            { err = ERR_INVALID_DATA; break; }
        cells[i].count = count;
        cells[i].object_off = n_objects;
        p += 6;
        for (uint j = 0; j < count; j++)
        {
            p = strstr(p,"<obj ");
            uint a0, a1, a2;
            if (!p || sscanf(p,"<obj attr0=\"0x%x\" attr1=\"0x%x\" attr2=\"0x%x\"/>",
                &a0,&a1,&a2) != 3 || a0 > 0xffff || a1 > 0xffff || a2 > 0xffff)
                { err = ERR_INVALID_DATA; break; }
            if (n_objects == obj_alloc)
            {
                const uint new_alloc = obj_alloc ? obj_alloc*2 : 64;
                u16 *new_objects = REALLOC(objects,3*new_alloc*sizeof(*objects));
                if (!new_objects) { err = ERR_CANT_CREATE; break; }
                objects = new_objects; obj_alloc = new_alloc;
            }
            objects[3*n_objects] = a0; objects[3*n_objects+1] = a1;
            objects[3*n_objects+2] = a2; n_objects++; p += 5;
        }
        if (err) break;
    }
    const u64 chunk64 = ( 0x20ull + 8ull*n_cells + 6ull*n_objects + 3 ) & ~3ull;
    if (!err && (chunk64 > UINT_MAX || chunk64+0x10 > UINT_MAX)) err = ERR_FILE_TOO_BIG;
    u8 *out = !err ? CALLOC(1,0x10+(uint)chunk64) : 0;
    if (!err && !out) err = ERR_CANT_CREATE;
    if (!err)
    {
        memcpy(out,"RECN",4); write_le16(out+4,0xfeff); write_le16(out+6,0x100);
        write_le32(out+8,0x10+(uint)chunk64); write_le16(out+12,0x10); write_le16(out+14,1);
        u8 *kbec = out+0x10;
        memcpy(kbec,"KBEC",4); write_le32(kbec+4,chunk64); write_le16(kbec+8,n_cells);
        write_le32(kbec+12,0x18); // cell table is at KBEC+0x20
        u8 *obj = kbec+0x20+8*n_cells;
        for (uint i = 0; i < n_cells; i++)
        {
            u8 *cell = kbec+0x20+8*i;
            write_le16(cell,cells[i].count); write_le32(cell+4,6*cells[i].object_off);
        }
        for (uint i = 0; i < n_objects; i++)
        {
            write_le16(obj+6*i,objects[3*i]); write_le16(obj+6*i+2,objects[3*i+1]);
            write_le16(obj+6*i+4,objects[3*i+2]);
        }
        if (verbose >= 0 || testmode)
            fprintf(stdlog,"%sCREATE NCER XML:%s -> %s\n",testmode ? "WOULD " : "",source,dest);
        if (!testmode)
        {
            File_t F;
            err = CreateFileOpt(&F,true,dest,false,source);
            if (F.f && fwrite(out,1,0x10+(uint)chunk64,F.f) != 0x10+(uint)chunk64)
                err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing NCER failed: %s\n",dest);
            ResetFile(&F,opt_preserve);
        }
    }
    FREE(out); FREE(objects); FREE(cells); FREE(xml);
    return err;
}

typedef struct nanr_xml_frame_t
{
    uint cell, duration, data_off;
}
nanr_xml_frame_t;

static enumError create_nanr_xml ( ccp source, ccp dest )
{
    u8 *xml = 0;
    size_t xml_size = 0;
    enumError err = LoadFileAlloc(source,0,0,&xml,&xml_size,16<<20,0,0,false);
    if (err) return err;
    uint n_anims = 0, n_frames = 0;
    if (sscanf((ccp)xml,"<?xml version=\"1.0\"?>\n<nanr animations=\"%u\" frames=\"%u\">",
        &n_anims,&n_frames) != 2 || !n_anims || !n_frames || n_anims > 65535 || n_frames > 65535)
        { FREE(xml); return ERR_INVALID_DATA; }
    uint *anim_first = CALLOC(n_anims,sizeof(*anim_first));
    uint *anim_count = CALLOC(n_anims,sizeof(*anim_count));
    nanr_xml_frame_t *frames = CALLOC(n_frames,sizeof(*frames));
    if (!anim_first || !anim_count || !frames) err = ERR_CANT_CREATE;
    uint frame_used = 0, data_size = 0;
    ccp p = (ccp)xml;
    for (uint i = 0; !err && i < n_anims; i++)
    {
        p = strstr(p,"<animation ");
        uint index, count;
        if (!p || sscanf(p,"<animation index=\"%u\" frames=\"%u\">",&index,&count) != 2
            || index != i || !count || count > n_frames-frame_used)
            { err = ERR_INVALID_DATA; break; }
        anim_first[i] = frame_used; anim_count[i] = count; p += 11;
        for (uint j = 0; j < count; j++)
        {
            p = strstr(p,"<frame ");
            uint cell, duration, off;
            if (!p || sscanf(p,"<frame cell=\"%u\" duration=\"%u\" data-offset=\"0x%x\"/>",
                &cell,&duration,&off) != 3 || cell > 0xffff || duration > 0xffff
                || off > 0xfffffffdu || off+2 < off)
                { err = ERR_INVALID_DATA; break; }
            frames[frame_used++] = (nanr_xml_frame_t){ cell, duration, off };
            if (data_size < off+2) data_size = off+2;
            p += 7;
        }
    }
    if (!err && frame_used != n_frames) err = ERR_INVALID_DATA;
    const u64 data_base = 0x20ull + 16ull*n_anims + 8ull*n_frames;
    const u64 chunk64 = (data_base + data_size + 3) & ~3ull;
    if (!err && (chunk64 > UINT_MAX || chunk64+0x10 > UINT_MAX)) err = ERR_FILE_TOO_BIG;
    u8 *out = !err ? CALLOC(1,0x10+(uint)chunk64) : 0;
    if (!err && !out) err = ERR_CANT_CREATE;
    if (!err)
    {
        memcpy(out,"RNAN",4); write_le16(out+4,0xfeff); write_le16(out+6,0x100);
        write_le32(out+8,0x10+(uint)chunk64); write_le16(out+12,0x10); write_le16(out+14,1);
        u8 *knba = out+0x10;
        memcpy(knba,"KNBA",4); write_le32(knba+4,chunk64);
        write_le16(knba+8,n_anims); write_le16(knba+10,n_frames);
        write_le32(knba+12,0x18); write_le32(knba+16,0x18+16*n_anims);
        write_le32(knba+20,0x18+16*n_anims+8*n_frames);
        u8 *anims = knba+0x20, *frame_ptr = anims+16*n_anims, *frame_data = frame_ptr+8*n_frames;
        for (uint i = 0; i < n_anims; i++)
        {
            write_le32(anims+16*i,anim_count[i]); write_le16(anims+16*i+6,1);
            write_le32(anims+16*i+8,1); write_le32(anims+16*i+12,8*anim_first[i]);
        }
        for (uint i = 0; i < n_frames; i++)
        {
            write_le32(frame_ptr+8*i,frames[i].data_off);
            write_le16(frame_ptr+8*i+4,frames[i].duration);
            write_le16(frame_data+frames[i].data_off,frames[i].cell);
        }
        if (verbose >= 0 || testmode)
            fprintf(stdlog,"%sCREATE NANR XML:%s -> %s\n",testmode ? "WOULD " : "",source,dest);
        if (!testmode)
        {
            File_t F;
            err = CreateFileOpt(&F,true,dest,false,source);
            if (F.f && fwrite(out,1,0x10+(uint)chunk64,F.f) != 0x10+(uint)chunk64)
                err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing NANR failed: %s\n",dest);
            ResetFile(&F,opt_preserve);
        }
    }
    FREE(out); FREE(frames); FREE(anim_count); FREE(anim_first); FREE(xml);
    return err;
}

static enumError encode_byml_file ( ccp source, ccp dest )
{
    u8 *text = 0;
    size_t text_len = 0;
    enumError err = LoadFileAlloc(source,0,0,&text,&text_len,16<<20,0,0,false);
    if (err) return err;
    u8 *byml = 0;
    uint byml_size = 0;
    ccp ext = strrchr(dest,'.');
    bool is_le = true;
    if (ext && !strcasecmp(ext,".be")) is_le = false;
    err = EncodeBYML_Text(&byml,&byml_size,(const char*)text,(uint)text_len,is_le,1);
    FREE(text);
    if (err) return err;
    if (!testmode)
    {
        File_t F;
        err = CreateFileOpt(&F,true,dest,false,source);
        if (F.f && fwrite(byml,1,byml_size,F.f) != byml_size)
            err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing BYML failed: %s\n",dest);
        ResetFile(&F,opt_preserve);
    }
    FREE(byml);
    return err;
}

static enumError cmd_create ( bool create )
{
    static const char dest_fname[] = "\1P/\1N\1?T";
    CheckOptDest(dest_fname,false);
    const bool save_auto_add = opt_auto_add;
    enumError max_err = ERR_OK;

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	opt_auto_add = save_auto_add; // restore auto-add settings

	ccp arg = plist.field[argi];
	const uint arg_len = strlen(arg);
	if (create && arg_len > 9 && !strcasecmp(arg+arg_len-9,".ncer.xml"))
	{
	    char xml_dest[PATH_MAX];
	    if (opt_dest)
		SubstDest(xml_dest,sizeof(xml_dest),arg,opt_dest,0,".ncer",false);
	    else
	    {
		snprintf(xml_dest,sizeof(xml_dest),"%s",arg);
		xml_dest[arg_len-4] = 0; // remove only .xml, retain .ncer
	    }
	    const enumError xml_err = create_ncer_xml(arg,xml_dest);
	    if (max_err < xml_err) max_err = xml_err;
	    continue;
	}
	if (create && arg_len > 9 && !strcasecmp(arg+arg_len-9,".nanr.xml"))
	{
	    char xml_dest[PATH_MAX];
	    if (opt_dest)
		SubstDest(xml_dest,sizeof(xml_dest),arg,opt_dest,0,".nanr",false);
	    else
	    {
		snprintf(xml_dest,sizeof(xml_dest),"%s",arg);
		xml_dest[arg_len-4] = 0;
	    }
	    const enumError xml_err = create_nanr_xml(arg,xml_dest);
	    if (max_err < xml_err) max_err = xml_err;
	    continue;
	}
	if (create && arg_len > 10 && !strcasecmp(arg+arg_len-10,".byml.yaml"))
	{
	    char y_dest[PATH_MAX];
	    if (opt_dest)
		SubstDest(y_dest,sizeof(y_dest),arg,opt_dest,0,".byml",false);
	    else
	    {
		snprintf(y_dest,sizeof(y_dest),"%s",arg);
		y_dest[arg_len-5] = 0;
	    }
	    const enumError y_err = encode_byml_file(arg,y_dest);
	    if (verbose >= 0 || testmode)
		fprintf(stdlog,"%s%sCREATE BYML %s -> %s\n",
		    verbose > 0 ? "\n" : "",testmode ? "WOULD " : "",
		    arg,y_dest);
	    if (max_err < y_err) max_err = y_err;
	    continue;
	}
	int src_len = strlen(arg);
	while ( src_len > 0 && arg[src_len-1] == '/' )
	    src_len--;
	((char*)arg)[src_len] = 0;
	char source_dir[PATH_MAX];
	snprintf(source_dir,sizeof(source_dir),"%s",arg);

	SetupParam_t sp;
	InitializeSetupParam(&sp);
	ScanSetupParam(&sp,true,arg,SZS_SETUP_FILE,0,true);

	char dest[PATH_MAX];
	SubstDest(dest,sizeof(dest),arg,opt_dest,dest_fname,
		GetExtFF(sp.fform_file,sp.fform_arch),false);
	ccp ext = strrchr(dest,'.');
	const bool sarc_le = ext && ( !strcasecmp(ext,".sarcle") || !strcasecmp(ext,".le")
		&& strlen(dest) >= 8 && !strcasecmp(ext-5,".sarc.le") );
	if (create && ext && ( !strcasecmp(ext,".sarc") || sarc_le ))
	{
	    enumError err = create_sarc_dir(source_dir,dest,!sarc_le);
	    if (verbose >= 0 || testmode)
		fprintf(stdlog,"%s%sCREATE %s SARC %s/ -> %s\n",
		    verbose > 0 ? "\n" : "",testmode ? "WOULD " : "",
		    sarc_le ? "little-endian" : "big-endian", source_dir,dest);
	    if (max_err < err) max_err = err;
	    ResetSetupParam(&sp);
	    continue;
	}
	if (create && ext && ( !strcasecmp(ext,".narc") || !strcasecmp(ext,".carc") ))
	{
	    enumError err = create_narc_dir(source_dir,dest,true);
	    if (verbose >= 0 || testmode)
		fprintf(stdlog,"%s%sCREATE NARC %s/ -> %s\n",
		    verbose > 0 ? "\n" : "",testmode ? "WOULD " : "",
		    source_dir,dest);
	    if (max_err < err) max_err = err;
	    ResetSetupParam(&sp);
	    continue;
	}
	if (create && ext && !strcasecmp(ext,".darc"))
	{
	    enumError err = create_darc_dir(source_dir,dest);
	    if (verbose >= 0 || testmode)
		fprintf(stdlog,"%s%sCREATE DARC %s/ -> %s\n",
		    verbose > 0 ? "\n" : "",testmode ? "WOULD " : "",
		    source_dir,dest);
	    if (max_err < err) max_err = err;
	    ResetSetupParam(&sp);
	    continue;
	}
	if (create && ext && ( !strcasecmp(ext,".pac") || !strcasecmp(ext,".pcs") ))
	{
	    enumError err = create_pac_dir(source_dir,dest);
	    if (verbose >= 0 || testmode)
		fprintf(stdlog,"%s%sCREATE PAC %s/ -> %s\n",
		    verbose > 0 ? "\n" : "",testmode ? "WOULD " : "",
		    source_dir,dest);
	    if (max_err < err) max_err = err;
	    ResetSetupParam(&sp);
	    continue;
	}
	if (create && ext && !strcasecmp(ext,".gfa"))
	{
	    enumError err = create_gfa_dir(source_dir,dest);
	    if (verbose >= 0 || testmode)
		fprintf(stdlog,"%s%sCREATE GFA %s/ -> %s\n",
		    verbose > 0 ? "\n" : "",testmode ? "WOULD " : "",
		    source_dir,dest);
	    if (max_err < err) max_err = err;
	    ResetSetupParam(&sp);
	    continue;
	}
	if (create && ext && ( !strcasecmp(ext,".byml") || !strcasecmp(ext,".byaml") ))
	{
	    enumError err = encode_byml_file(arg,dest);
	    if (verbose >= 0 || testmode)
		fprintf(stdlog,"%s%sCREATE BYML %s -> %s\n",
		    verbose > 0 ? "\n" : "",testmode ? "WOULD " : "",
		    arg,dest);
	    if (max_err < err) max_err = err;
	    ResetSetupParam(&sp);
	    continue;
	}

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = CreateSZS(&szs,dest,arg,0,&sp,0,
			verbose > 0 ? UINT_MAX : 0, false );

	//--- create file

	if ( verbose >= 0 || testmode )
	{
	    if (create)
		fprintf(stdlog,"%s%sCREATE %s/ -> %s:%s\n",
			verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "",
			arg,
			GetNameFF_SZS(&szs),
			dest );
	    else
		fprintf(stdlog,"%s%sENCODE %s/\n",
			verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "",
			arg );
	    fflush(stdlog);
	}

	if ( create && err <= ERR_WARNING && err != ERR_NOT_EXISTS )
	{
	    File_t F;
	    CreateFileOpt(&F,true,dest,testmode,0);
	    if (F.f)
	    {
		SetFileAttrib(&F.fatt,&szs.fatt,0);
		const u8 *   data = szs.cdata ? szs.cdata : szs.data;
		const size_t size = szs.cdata ? szs.csize : szs.size;
		const size_t wstat = fwrite(data,1,size,F.f);
		if ( wstat != size )
		    err = FILEERROR1(&F,ERR_WRITE_FAILED,
				"Writing %zu bytes failed: %s\n",
				size, dest);
	    }
	    ResetFile(&F,opt_preserve);
	    LinkCacheSZS(&szs,dest);
	}

	if ( max_err < err )
	     max_err = err;
	ResetSZS(&szs);
	ResetSetupParam(&sp);
    }

    ResetStringField(&plist);
    opt_auto_add = save_auto_add; // restore auto-add settings
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command update			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct update_param_t
{
    ccp			source;		// source path
    bool		update_sub;	// true: update sub archives
    bool		encode;		// true: encode files if possible
    int			recurse_level;	// current recurse level
    int			indent;		// indention of log messages
}
update_param_t;

//-----------------------------------------------------------------------------

static enumError job_update
(
    szs_file_t	*szs,		// valid szs file
    int		recurse_level	// recursion level
);

///////////////////////////////////////////////////////////////////////////////

static int update_func
(
    struct szs_iterator_t	*it,	// iterator struct with all infos
    bool			term	// true: termination hint
)
{
    if (term)
	return 0;

    DASSERT(it);
    DASSERT(it->param);
    szs_file_t * szs = it->szs;
    DASSERT(szs);
    update_param_t * up = it->param;
    DASSERT(up);
    DASSERT(up->source);


    //--- ignore directories

    if (it->is_dir)
	return 0;


    //--- setup path

    ccp local_path = it->path;
    if ( *local_path == '.' && local_path[1] == '/' )
	local_path += 2;

    char path_buf[PATH_MAX];
    ccp path = PathCatPP(path_buf,sizeof(path_buf),up->source,local_path);

    u8 * data = szs->data + it->off;
// [[analyse-magic]]
    file_format_t fform = GetByMagicFF(data,it->size,it->size);


    //--- [2do] encode files first


    //--- read updated file

    struct stat st;
    if (!stat(path,&st))
    {
	if (!S_ISREG(st.st_mode))
	{
	    if (!opt_ignore)
		ERROR0(ERR_WARNING,"No regular file: %s\n",path);
	}
	else if ( st.st_size != it->size )
	{
	    if (!opt_ignore)
		ERROR0(ERR_WARNING,
			"File size mismatch: have %llu, need %u: %s\n",
			(u64)st.st_size ,it->size, path );
	}
	else
	{
	    if ( verbose > 1 )
		fprintf(stdlog,"%*s- LOAD %s\n", up->indent,"", path );
	    LoadFILE(path,0,0,data,it->size,0,0,false);
	}
    }


    //--- update sub archives first

    if ( up->update_sub && IsArchiveFF(fform) )
    {
	szs_file_t subszs;	    // [[2do]] [[subfile]]
	InitializeSZS(&subszs);
	subszs.data		= data ;
	subszs.size		= it->size;
	subszs.fname		= path;
	subszs.fform_file	= fform;
	subszs.fform_arch	= fform;
	job_update(&subszs,up->recurse_level+1);
	subszs.fname		= EmptyString;
	ResetSZS(&subszs);
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

static enumError job_update
(
    szs_file_t	*szs,		// valid szs file
    int		recurse_level	// recursion level
)
{
    DASSERT(szs);
    MEM_CHECK;
    PRINT("JOB UPDATE: %d/%d %s\n",recurse_level,opt_recurse,szs->fname);

    char source[PATH_MAX];
    StringCat2S(source,sizeof(source),szs->fname,".d");
    if ( !recurse_level && ( opt_source || !IsDirectory(source,false) ) )
    {
	ccp my_source = opt_source;
	if (!my_source)
	{
	    switch (szs->fform_arch)
	    {
		case FF_U8:
		case FF_WU8:
// [[lta]] [[2do]]
// [[lfl]] [[2do]]
		case FF_RARC:
		case FF_PACK:
		case FF_RKC:
		//case FF_BRRES:
		    my_source = "\1P/\1N.d/";
		    break;

		default:
		    my_source = "\1P/\1F.d/";
		    break;
	    }
	}
	SubstDest(source,sizeof(source),szs->fname,my_source,0,0,true);
    }

    const int indent = 2 * recurse_level;
    if ( verbose >= 0 && !recurse_level || verbose > 0 || testmode )
	fprintf(stdlog,"%s%*s- UPDATE %s:%s <- %s\n",
		verbose > 1 ? "\n" : "",
		indent,"",
		GetNameFF_SZS(szs), szs->fname, source );


    update_param_t up;
    memset(&up,0,sizeof(up));
    up.source		= source;
    up.encode		= !opt_no_encode;
    up.recurse_level	= recurse_level;
    up.update_sub	= recurse_level < opt_recurse;
    up.indent		= indent + 2;

    IterateFilesParSZS(szs,update_func,&up,false,false,false,0,-1,SORT_NONE);
    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_update()
{
    if ( opt_source && !*opt_source )
	opt_source = 0;
    if ( opt_dest && !*opt_dest )
	opt_dest = 0;
    opt_mkdir = true;

    enumError max_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,false);
	if (!err)
	    err = job_update(&szs,0);
	if ( max_err < err )
	     max_err = err;

	if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS )
	{
	    char dest[PATH_MAX];
	    SubstDest(dest,sizeof(dest),arg,opt_dest,0,0,false);

	    if ( verbose >= 0 || testmode )
	    {
		fprintf(stdlog,"%s%sCREATE %s:%s\n",
			verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "",
			GetNameFF_SZS(&szs),
			dest );
		fflush(stdlog);
	    }

	    File_t F;
	    CreateFileOpt(&F,true,dest,testmode,arg);
	    if (F.f)
	    {
		if (IsCompressedFF(szs.fform_file))
		    CompressSZS(&szs,0,true);
		SetFileAttrib(&F.fatt,&szs.fatt,0);
		const u8 *   data = szs.cdata ? szs.cdata : szs.data;
		const size_t size = szs.cdata ? szs.csize : szs.size;
		const size_t wstat = fwrite(data,1,size,F.f);
		if ( wstat != size )
		    err = FILEERROR1(&F,ERR_WRITE_FAILED,
				"Writing %zu bytes failed: %s\n",
				size, dest);
	    }
	    ResetFile(&F,opt_preserve);
	}

	ResetSZS(&szs);
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command extract			///////////////
///////////////////////////////////////////////////////////////////////////////

static bool valid_sarc_path ( ccp path )
{
    return path && *path && path[0] != '/' && !strchr(path,'\\')
        && strncmp(path,"../",3) && strcmp(path,"..") && !strstr(path,"/../");
}

// Forward declared so SARC/PAC/GFA extraction can recurse into their own
// output directory once all members are written -- without this, anything
// nested inside those container types (e.g. the .brres models inside a real
// Kirby's Epic Yarn disc's .gfa archives) was written to disk but never fed
// back through the XX pipeline, so it never got textures decoded or a DAE
// exported. Pass-through staging (wit/ndstool/etc.) already did this via
// extract_tree(); the three native container extractors below did not.
static enumError extract_tree ( ccp root, uint depth );
static enumError extract_tree_complete ( ccp root, uint depth );
static enumError decode_image_if_possible ( ccp arg );

// SubstDest() with a NULL/empty 'dest' param just echoes 'arg' back
// unchanged -- it returns before ever touching the "\1P/\1N" pattern (see
// its own early-return). extract_tree()'s recursion into pass-through
// staged files deliberately passes opt_dest=0 ("each decoded file belongs
// beside its own source"), so every one of SARC/PAC/GFA's "\1P/\1N"
// SubstDest() calls degenerated to "extract into a directory with the
// exact same path as the source file itself" -- a guaranteed "Not a
// directory" write failure the moment the first member is written. Only
// caught because a real Kirby's Epic Yarn WBFS exercises this exact path
// (2329 of 2342 on-disc .gfa archives go through here); the opt_dest-set
// case this was originally tested with never triggers SubstDest()'s
// early-return, so it looked fine in isolation.
static void beside_source_dest ( char *dest, uint dest_size, ccp arg )
{
    if (opt_dest)
	SubstDest(dest,dest_size,arg,opt_dest,"\1P/\1N",0,false);
    else
	snprintf(dest,dest_size,"%s.d",arg);
}

static enumError extract_sarc_mem ( ccp arg, ccp basedir, uint depth, const u8 *raw, size_t raw_size )
{
    if (!raw || raw_size < 0x20 || raw_size > UINT_MAX || memcmp(raw,"SARC",4))
        return ERR_NOTHING_TO_DO;
    nintendo_sarc_t sarc;
    enumError err = ScanSARC(&sarc,raw,raw_size);
    if (err) return ERR_NOTHING_TO_DO;

    char dest[PATH_MAX];
    beside_source_dest(dest,sizeof(dest),arg);
    if (verbose >= 0 || testmode)
        fprintf(stdlog,"%s%sEXTRACT SARC:%s (%u files) -> %s/\n",
            verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
            arg,sarc.n_entries,dest);
    for (uint i = 0; !err && i < sarc.n_entries; i++)
    {
        ccp name = 0;
        const u8 *data = 0;
        uint size = 0;
        err = GetSARCEntry(&sarc,i,&name,&data,&size);
        char auto_name[PATH_MAX];
        if (!err && (!name || !*name))
        {
            ccp ext = ".bin";
            if (size >= 4)
            {
                if (!memcmp(data,"CTPK",4)) ext = ".ctpk";
                else if (!memcmp(data,"CGFX",4)) ext = ".cgfx";
                else if (!memcmp(data,"CLYT",4)) ext = ".bclyt";
                else if (!memcmp(data,"CLAN",4)) ext = ".bclan";
                else if (!memcmp(data,"BCTR",4)) ext = ".bctr";
                else if (!memcmp(data,"BSEQ",4)) ext = ".bseq";
                else if (!memcmp(data,"DVLB",4)) ext = ".dvlb";
                else if (!memcmp(data,"SPBD",4)) ext = ".spbd";
                else if (size >= 0x28 && (!memcmp(data+size-0x28,"CLIM",4) || !memcmp(data+size-0x28,"FLIM",4))) ext = ".bclim";
            }
            snprintf(auto_name,sizeof(auto_name),"file_%04u%s",i,ext);
            name = auto_name;
        }
        if (!err && !valid_sarc_path(name))
            err = ERROR0(ERR_INVALID_DATA,"Unsafe SARC entry path: %s\n",name);
        if (err || testmode) continue;
        char path[PATH_MAX];
        snprintf(path,sizeof(path),"%s/%s%s",dest,basedir ? basedir : "",name);
        File_t F;
        err = CreateFileOpt(&F,true,path,false,arg);
        if (F.f && fwrite(data,1,size,F.f) != size)
            err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing %u bytes failed: %s\n",size,path);
        ResetFile(&F,opt_preserve);
    }
    if ( !err && !testmode )
    {
        enumError sub_err = extract_tree_complete(dest,depth+1);
        if ( err < sub_err ) err = sub_err;
    }
    return err;
}

static enumError extract_sarc_file ( ccp arg, ccp basedir, uint depth )
{
    if ( !is_ext(arg,".sarc") && !is_ext(arg,".szs") && !is_ext(arg,".lyarc") && !is_ext(arg,".arc") && !is_ext(arg,".pack") && !is_ext(arg,".bin") )
	return ERR_NOTHING_TO_DO;

    u8 *raw = 0;
    size_t raw_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&raw,&raw_size,0,0,0,false);
    if (err) return ERR_NOTHING_TO_DO;
    if (raw_size > UINT_MAX) { FREE(raw); return ERR_FILE_TOO_BIG; }
    err = extract_sarc_mem(arg,basedir,depth,raw,raw_size);
    FREE(raw);
    return err;
}

static enumError extract_ctpk_mem ( ccp arg, ccp basedir, uint depth, const u8 *raw, size_t raw_size )
{
    if (!raw || raw_size < 0x20 || raw_size > UINT_MAX || memcmp(raw,"CTPK",4))
        return ERR_NOTHING_TO_DO;
    nintendo_ctpk_t ctpk;
    enumError err = ScanCTPK(&ctpk,raw,raw_size);
    if (err) return ERR_NOTHING_TO_DO;

    char dest[PATH_MAX];
    beside_source_dest(dest,sizeof(dest),arg);
    if (verbose >= 0 || testmode)
        fprintf(stdlog,"%s%sEXTRACT CTPK:%s (%u textures) -> %s/\n",
            verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
            arg,ctpk.n_entries,dest);

    for (uint i = 0; !err && i < ctpk.n_entries; i++)
    {
        nintendo_ctpk_entry_t entry;
        err = GetCTPKEntry(&ctpk,i,&entry);
        if (err || testmode) continue;

        u8 *rgba = 0;
        uint w = 0, h = 0;
        err = DecodeCTPKTexture_RGBA(&rgba,&w,&h,&entry);
        if (err) continue;

        char sub_name[PATH_MAX];
        if (entry.name[0])
        {
            ccp slash = strrchr(entry.name, '/');
            ccp bslash = strrchr(entry.name, '\\');
            ccp base = slash && bslash ? (slash > bslash ? slash + 1 : bslash + 1)
                     : slash ? slash + 1
                     : bslash ? bslash + 1
                     : entry.name;
            char clean_base[PATH_MAX];
            snprintf(clean_base,sizeof(clean_base),"%s",base);
            char *dot = strrchr(clean_base,'.');
            if (dot) *dot = 0;
            snprintf(sub_name,sizeof(sub_name),"%s.png",clean_base);
        }
        else
        {
            snprintf(sub_name,sizeof(sub_name),"tex_%03u.png",i);
        }

        char out_path[PATH_MAX];
        snprintf(out_path,sizeof(out_path),"%s/%s%s",dest,basedir ? basedir : "",sub_name);

        Image_t img;
        InitializeIMG(&img);
        const uint xw = EXPAND8(w), xh = EXPAND8(h);
        u8 *padded = xw==w && xh==h ? rgba : CALLOC(1,xw*xh*4);
        if (padded != rgba)
        {
            for (uint y = 0; y < h; y++)
                memcpy(padded+y*xw*4,rgba+y*w*4,w*4);
            FREE(rgba);
        }
        img.data = padded;
        img.data_alloced = true;
        img.data_size = xw * xh * 4;
        img.width = w; img.xwidth = xw;
        img.height = h; img.xheight = xh;
        img.iform = img.info_iform = IMG_X_RGB;
        img.info_fform = FF_PNG;
        img.info_n_image = 1;
        img.endian = &le_func;

        err = SavePNG(&img,false,0,out_path,0,0,opt_overwrite>0,0);
        ResetIMG(&img);
    }
    if (!err && !testmode)
    {
        enumError sub_err = extract_tree_complete(dest,depth+1);
        if (err < sub_err) err = sub_err;
    }
    return err;
}

static enumError extract_ctpk_file ( ccp arg, ccp basedir, uint depth )
{
    if ( !is_ext(arg,".ctpk") && !is_ext(arg,".bin") )
	return ERR_NOTHING_TO_DO;

    u8 *raw = 0;
    size_t raw_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&raw,&raw_size,0,0,0,false);
    if (err) return ERR_NOTHING_TO_DO;
    if (raw_size > UINT_MAX) { FREE(raw); return ERR_FILE_TOO_BIG; }
    err = extract_ctpk_mem(arg,basedir,depth,raw,raw_size);
    FREE(raw);
    return err;
}

// Extract a Brawl PAC archive ("ARC\0"). Unlike SARC/GFA, entries have no
// filename -- only a numeric type/index/group -- so members are written out
// as "<index>_<group>.<type-name>.bin", e.g. "0000_00.MiscData.bin".
static const char *pac_type_name ( u16 type )
{
    switch (type)
    {
	case 1: return "MiscData";
	case 2: return "ModelData";
	case 3: return "TextureData";
	case 4: return "AnimationData";
	case 5: return "SceneData";
	case 6: return "Type6";
	case 7: return "GroupedArchive";
	case 8: return "EffectData";
	default: return "Unknown";
    }
}

static enumError extract_pac_file ( ccp arg, ccp basedir, uint depth )
{
    if ( !is_ext(arg,".pac") && !is_ext(arg,".pcs") && !is_ext(arg,".arc") )
	return ERR_NOTHING_TO_DO;

    u8 *raw = 0;
    size_t raw_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&raw,&raw_size,0,0,0,false);
    if (err) return ERR_NOTHING_TO_DO;
    if ( raw_size > UINT_MAX ) { FREE(raw); return ERR_FILE_TOO_BIG; }
    if ( raw_size < 4 || memcmp(raw,"ARC\0",4) ) { FREE(raw); return ERR_NOTHING_TO_DO; }

    pac_t pac;
    err = ScanPAC(&pac,raw,raw_size);
    if (err) { FREE(raw); return ERR_NOTHING_TO_DO; }

    char dest[PATH_MAX];
    beside_source_dest(dest,sizeof(dest),arg);
    if ( verbose >= 0 || testmode )
	fprintf(stdlog,"%s%sEXTRACT PAC:%s (%u entries, name=%s) -> %s/\n",
	    verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
	    arg, pac.n_entries, pac.name, dest );

    for ( uint i = 0; !err && i < pac.n_entries; i++ )
    {
	const pac_entry_t *e = pac.entries+i;
	if (testmode) continue;

	char path[PATH_MAX];
	snprintf(path,sizeof(path),"%s/%s%04u_%02u.%s.bin",
	    dest,basedir ? basedir : "",e->index,e->group_index,pac_type_name(e->type));
	File_t F;
	err = CreateFileOpt(&F,true,path,false,arg);
	if ( F.f && e->size && fwrite(e->data,1,e->size,F.f) != e->size )
	    err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing %u bytes failed: %s\n",e->size,path);
	ResetFile(&F,opt_preserve);
    }

    ResetPAC(&pac);
    FREE(raw);
    if ( !err && !testmode )
    {
        enumError sub_err = extract_tree_complete(dest,depth+1);
        if ( err < sub_err ) err = sub_err;
    }
    return err;
}

// Extract a Good-Feel archive (GFAC).  The whole payload is one GFCP-
// compressed blob; ScanGFA decompresses it and returns the member table.
// Entries with size 0 are directory markers: the reference tooling treats
// each as the parent directory for the entries that follow it.
static enumError extract_gfa_file ( ccp arg, ccp basedir, uint depth )
{
    if ( !is_ext(arg,".gfa") && !is_ext(arg,".bin") )
	return ERR_NOTHING_TO_DO;

    u8 *raw = 0;
    size_t raw_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&raw,&raw_size,0,0,0,false);
    if (err) return ERR_NOTHING_TO_DO;
    if ( raw_size > UINT_MAX ) { FREE(raw); return ERR_FILE_TOO_BIG; }
    if ( raw_size < 4 || memcmp(raw,"GFAC",4) ) { FREE(raw); return ERR_NOTHING_TO_DO; }

    gfa_t gfa;
    err = ScanGFA(&gfa,raw,raw_size);
    FREE(raw);
    if (err) return err;

    char dest[PATH_MAX];
    beside_source_dest(dest,sizeof(dest),arg);
    if ( verbose >= 0 || testmode )
	fprintf(stdlog,"%s%sEXTRACT GFA:%s (%u entries, %s) -> %s/\n",
	    verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
	    arg, gfa.n_entries,
	    gfa.compression == 1 ? "BPE" : "LZ10", dest );

    ccp subdir = "";
    for ( uint i = 0; !err && i < gfa.n_entries; i++ )
    {
	const gfa_entry_t *e = gfa.entries+i;
	if (!e->size)
	{
	    // directory marker
	    subdir = e->name;
	    continue;
	}
	char rel[PATH_MAX];
	if ( subdir && *subdir )
	    snprintf(rel,sizeof(rel),"%s/%s",subdir,e->name);
	else
	    snprintf(rel,sizeof(rel),"%s",e->name);
	if (!valid_sarc_path(rel))
	{
	    err = ERROR0(ERR_INVALID_DATA,"Unsafe GFA entry path: %s\n",rel);
	    break;
	}
	if (testmode) continue;

	char path[PATH_MAX];
	snprintf(path,sizeof(path),"%s/%s%s",dest,basedir ? basedir : "",rel);
	File_t F;
	err = CreateFileOpt(&F,true,path,false,arg);
	if ( F.f && fwrite(gfa.blob+e->offset,1,e->size,F.f) != e->size )
	    err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing %u bytes failed: %s\n",e->size,path);
	ResetFile(&F,opt_preserve);
    }

    ResetGFA(&gfa);
    if ( !err && !testmode )
    {
        enumError sub_err = extract_tree_complete(dest,depth+1);
        if ( err < sub_err ) err = sub_err;
    }
    return err;
}

static enumError extract_narc_mem ( ccp arg, ccp basedir, uint depth, const u8 *raw, size_t raw_size )
{
    if (!raw || raw_size < 16 || raw_size > UINT_MAX)
        return ERR_NOTHING_TO_DO;
    if (memcmp(raw,"NARC",4) && memcmp(raw,"CRAN",4))
        return ERR_NOTHING_TO_DO;

    narc_t narc;
    enumError err = ScanNARC(&narc,raw,raw_size);
    if (err) return ERR_NOTHING_TO_DO;

    char dest[PATH_MAX];
    beside_source_dest(dest,sizeof(dest),arg);
    if (verbose >= 0 || testmode)
        fprintf(stdlog,"%s%sEXTRACT NARC:%s (%u files) -> %s/\n",
            verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
            arg,narc.n_entries,dest);

    for (uint i = 0; !err && i < narc.n_entries; i++)
    {
        const narc_entry_t *e = narc.entries + i;
        if (!e->size) continue;
        char auto_name[PATH_MAX];
        ccp name = e->name;
        if (!name || !*name)
        {
            ccp ext = ".bin";
            if (e->offset + e->size <= narc.fimg_size)
            {
                const u8 *sub_d = narc.fimg_data + e->offset;
                if (e->size >= 4)
                {
                    if (!memcmp(sub_d,"BY",2) || !memcmp(sub_d,"YB",2)) ext = ".byml";
                    else if (!memcmp(sub_d,"CTPK",4)) ext = ".ctpk";
                    else if (!memcmp(sub_d,"CGFX",4)) ext = ".cgfx";
                    else if (!memcmp(sub_d,"CLYT",4)) ext = ".bclyt";
                    else if (!memcmp(sub_d,"CLAN",4)) ext = ".bclan";
                    else if (!memcmp(sub_d,"DVLB",4)) ext = ".dvlb";
                    else if (!memcmp(sub_d,"BMD0",4)) ext = ".nsbmd";
                    else if (!memcmp(sub_d,"BTX0",4)) ext = ".nsbtx";
                    else if (e->size >= 0x28 && (!memcmp(sub_d+e->size-0x28,"CLIM",4) || !memcmp(sub_d+e->size-0x28,"FLIM",4))) ext = ".bclim";
                }
            }
            snprintf(auto_name, sizeof(auto_name), "file_%04u%s", i, ext);
            name = auto_name;
        }

        if (!valid_sarc_path(name))
        {
            err = ERROR0(ERR_INVALID_DATA, "Unsafe NARC entry path: %s\n", name);
            break;
        }
        if (testmode) continue;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s%s", dest, basedir ? basedir : "", name);
        File_t F;
        err = CreateFileOpt(&F, true, path, false, arg);
        if (F.f && e->offset + e->size <= narc.fimg_size && fwrite(narc.fimg_data + e->offset, 1, e->size, F.f) != e->size)
            err = FILEERROR1(&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
        ResetFile(&F, opt_preserve);
    }

    ResetNARC(&narc);
    if (!err && !testmode)
    {
        enumError sub_err = extract_tree_complete(dest, depth + 1);
        if (err < sub_err) err = sub_err;
    }
    return err;
}

static enumError extract_narc_file ( ccp arg, ccp basedir, uint depth )
{
    if ( !is_ext(arg,".narc") && !is_ext(arg,".carc") && !is_ext(arg,".bin") )
	return ERR_NOTHING_TO_DO;

    u8 *raw = 0;
    size_t raw_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&raw,&raw_size,0,0,0,false);
    if (err) return ERR_NOTHING_TO_DO;
    if (raw_size > UINT_MAX) { FREE(raw); return ERR_FILE_TOO_BIG; }
    err = extract_narc_mem(arg,basedir,depth,raw,raw_size);
    FREE(raw);
    return err;
}

// Extract a DARC archive ("darc" magic) -- the 3DS/NW4C counterpart of SARC,
// found bundling a game's layout family into one romfs file (see the format
// comment above darc_t in lib-nintendo.h). Unlike PAC/GFA's flat member
// list, DARC's tree can nest arbitrarily deep (parent-index/end-index per
// entry), so this walks it with an explicit directory stack rather than
// GFA's single "current subdir" variable -- a nested dir ending resets to
// whatever directory is now on top of the stack, not to the root.
static enumError extract_darc_file ( ccp arg, ccp basedir, uint depth )
{
    if ( !is_ext(arg,".darc") && !is_ext(arg,".arc") && !is_ext(arg,".bin") )
	return ERR_NOTHING_TO_DO;

    u8 *raw = 0;
    size_t raw_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&raw,&raw_size,0,0,0,false);
    if (err) return ERR_NOTHING_TO_DO;
    if ( raw_size > UINT_MAX ) { FREE(raw); return ERR_FILE_TOO_BIG; }
    if ( raw_size < 4 || memcmp(raw,"darc",4) ) { FREE(raw); return ERR_NOTHING_TO_DO; }

    darc_t darc;
    err = ScanDARC(&darc,raw,(uint)raw_size);
    if (err) { FREE(raw); return ERR_NOTHING_TO_DO; }

    char dest[PATH_MAX];
    beside_source_dest(dest,sizeof(dest),arg);
    if ( verbose >= 0 || testmode )
	fprintf(stdlog,"%s%sEXTRACT DARC:%s (%u entries) -> %s/\n",
	    verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
	    arg, darc.n_entries, dest );

    // stack[k].path is the assembled relative path of the directory at
    // depth k (root = ""); stack[k].end is that directory's end-index.
    enum { MAX_DARC_DEPTH = 64 };
    struct { char path[PATH_MAX]; uint end; } stack[MAX_DARC_DEPTH];
    uint sp = 0;
    stack[0].path[0] = 0;
    stack[0].end = darc.n_entries;

    for ( uint i = 1; !err && i < darc.n_entries; i++ )
    {
	while ( sp > 0 && i >= stack[sp].end )
	    sp--;
	const darc_entry_t *e = darc.entries+i;

	if ( e->is_dir )
	{
	    // Entry 1 is commonly a "." alias of the root (per GBATEK); real
	    // subfolders never have an empty or "." name.
	    if ( !*e->name || !strcmp(e->name,".") )
		continue;
	    if ( sp+1 >= MAX_DARC_DEPTH || e->end_or_size > darc.n_entries )
		{ err = ERROR0(ERR_INVALID_DATA,"DARC nesting too deep or corrupt: %s\n",arg); break; }
	    char rel[PATH_MAX];
	    if ( *stack[sp].path )
		snprintf(rel,sizeof(rel),"%s/%s",stack[sp].path,e->name);
	    else
		snprintf(rel,sizeof(rel),"%s",e->name);
	    if (!valid_sarc_path(rel))
		{ err = ERROR0(ERR_INVALID_DATA,"Unsafe DARC entry path: %s\n",rel); break; }
	    sp++;
	    snprintf(stack[sp].path,sizeof(stack[sp].path),"%s",rel);
	    stack[sp].end = e->end_or_size;
	    continue;
	}

	char rel[PATH_MAX];
	if ( *stack[sp].path )
	    snprintf(rel,sizeof(rel),"%s/%s",stack[sp].path,e->name);
	else
	    snprintf(rel,sizeof(rel),"%s",e->name);
	if (!valid_sarc_path(rel))
	    { err = ERROR0(ERR_INVALID_DATA,"Unsafe DARC entry path: %s\n",rel); break; }
	if (testmode) continue;

	char path[PATH_MAX];
	snprintf(path,sizeof(path),"%s/%s%s",dest,basedir ? basedir : "",rel);
	File_t F;
	err = CreateFileOpt(&F,true,path,false,arg);
	if ( F.f && e->end_or_size
	    && fwrite(raw+e->parent_or_offset,1,e->end_or_size,F.f) != e->end_or_size )
	    err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing %u bytes failed: %s\n",e->end_or_size,path);
	ResetFile(&F,opt_preserve);
    }

    ResetDARC(&darc);
    FREE(raw);
    if ( !err && !testmode )
    {
        enumError sub_err = extract_tree_complete(dest,depth+1);
        if ( err < sub_err ) err = sub_err;
    }
    return err;
}

// AT7 container archive extraction (Pokémon Mystery Dungeon WiiWare data*.bin / data*.raw / data*.at7).
// Either uncompressed raw data or compressed (AT7P/AT7X/AT7E) stream.
// Table starts at offset 0 with 28-byte (0x1C) records:
// uint32_be file_offset, uint32_be file_size, char file_name[20].
// Ends when file_offset == 0 or record matches namesEnd.
static enumError extract_at7_file ( ccp arg, ccp basedir, uint depth )
{
    // Unlike every sibling extract_*_file(), AT7 has no fixed extension, so
    // this runs against every file reaching extract_one_file() -- including
    // multi-gigabyte pass-through sources (e.g. a Switch Program NCA). Cap
    // the load with LoadFileAlloc's own max_size so those are rejected
    // cheaply instead of fully read into memory first. The raw EFBIG errno
    // (27) must never be returned here: it aliases ERU_WARNING in this
    // file's own enumError enum, which silently aborts the whole XX
    // pipeline with no message -- confirmed live on Super Mario Odyssey's
    // 5.5 GB Program NCA, which never reached the hactool pass-through
    // because of this.
    u8 *raw = 0;
    size_t raw_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&raw,&raw_size,UINT_MAX,2,0,false);
    if (err) return ERR_NOTHING_TO_DO;
    if ( raw_size < 28 ) { FREE(raw); return ERR_NOTHING_TO_DO; }

    u8 *decomp = 0;
    uint decomp_size = 0;
    if ( !memcmp(raw,"AT7P",4) || !memcmp(raw,"AT7X",4) )
    {
        err = DecodeAT7(&decomp,&decomp_size,raw,(uint)raw_size);
        FREE(raw);
        if (err) return ERR_NOTHING_TO_DO;
        raw = decomp;
        raw_size = decomp_size;
        if ( raw_size < 28 ) { FREE(raw); return ERR_NOTHING_TO_DO; }
    }

    // Check if raw is an AT7 table of contents
    uint n_entries = 0;
    uint cur = 0;
    while ( cur + 28 <= raw_size )
    {
        uint off = be32(raw+cur);
        uint sz  = be32(raw+cur+4);
        if ( !off ) break; // End of table
        if ( off < 28 || (u64)off + sz > raw_size )
            { FREE(raw); return ERR_NOTHING_TO_DO; }
        // Check filename characters
        bool valid_name = false;
        for ( uint k = 8; k < 28; k++ )
        {
            u8 ch = raw[cur+k];
            if ( !ch ) { if ( k > 8 ) valid_name = true; break; }
            if ( ch < 0x20 || ch > 0x7E ) break;
        }
        if ( !valid_name ) { FREE(raw); return ERR_NOTHING_TO_DO; }
        n_entries++;
        cur += 28;
    }

    if ( !n_entries ) { FREE(raw); return ERR_NOTHING_TO_DO; }

    char dest[PATH_MAX];
    beside_source_dest(dest,sizeof(dest),arg);
    if ( verbose >= 0 || testmode )
        fprintf(stdlog,"%s%sEXTRACT AT7:%s (%u entries) -> %s/\n",
            verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
            arg, n_entries, dest );

    cur = 0;
    for ( uint i = 0; !err && i < n_entries; i++, cur += 28 )
    {
        uint off = be32(raw+cur);
        uint sz  = be32(raw+cur+4);
        char name[24] = {0};
        memcpy(name, raw+cur+8, 20);
        name[20] = 0;
        if ( !valid_sarc_path(name) )
            { err = ERROR0(ERR_INVALID_DATA,"Unsafe AT7 entry path: %s\n",name); break; }
        if (testmode) continue;

        char path[PATH_MAX];
        snprintf(path,sizeof(path),"%s/%s%s",dest,basedir ? basedir : "",name);
        File_t F;
        err = CreateFileOpt(&F,true,path,false,arg);
        if ( F.f && sz && fwrite(raw+off,1,sz,F.f) != sz )
            err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing %u bytes failed: %s\n",sz,path);
        ResetFile(&F,opt_preserve);
    }

    FREE(raw);
    if ( !err && !testmode )
    {
        enumError sub_err = extract_tree_complete(dest,depth+1);
        if ( err < sub_err ) err = sub_err;
    }
    return err;
}

static void decompress_mpbin_lzss(u8 *out, uint out_size, const u8 *src, uint src_size)
{
    u8 window[1024] = {0};
    uint win_off = 958;
    uint dest_off = 0;
    uint src_off = 0;
    int code_word = 0;
    while (dest_off < out_size && src_off < src_size)
    {
        if ((code_word & 0x100) == 0)
        {
            if (src_off >= src_size) break;
            code_word = src[src_off++] | 0xFF00;
        }
        if (code_word & 1)
        {
            if (src_off >= src_size) break;
            u8 val = src[src_off++];
            out[dest_off++] = val;
            window[win_off] = val;
            win_off = (win_off + 1) % 1024;
        }
        else
        {
            if (src_off + 1 >= src_size) break;
            u8 b1 = src[src_off++];
            u8 b2 = src[src_off++];
            uint offset = ((b2 & 0xC0) << 2) | b1;
            uint copy_len = (b2 & 0x3F) + 3;
            for (uint i = 0; i < copy_len && dest_off < out_size; i++)
            {
                u8 val = window[(offset + i) % 1024];
                window[win_off] = val;
                win_off = (win_off + 1) % 1024;
                out[dest_off++] = val;
            }
        }
        code_word >>= 1;
    }
}

static void decompress_mpbin_slide(u8 *out, uint out_size, const u8 *src, uint src_size)
{
    uint dest_off = 0;
    uint src_off = 0;
    u32 code_word = 0;
    int bits_left = 0;
    while (dest_off < out_size && src_off < src_size)
    {
        if (bits_left == 0)
        {
            if (src_off + 4 > src_size) break;
            code_word = ((u32)src[src_off] << 24) | ((u32)src[src_off+1] << 16) | ((u32)src[src_off+2] << 8) | src[src_off+3];
            src_off += 4;
            bits_left = 32;
        }
        if (code_word & 0x80000000)
        {
            if (src_off >= src_size) break;
            out[dest_off++] = src[src_off++];
        }
        else
        {
            if (src_off + 1 >= src_size) break;
            u8 b1 = src[src_off++];
            u8 b2 = src[src_off++];
            uint dist_back = (((b1 & 0x0F) << 8) | b2) + 1;
            uint copy_len = ((b1 & 0xF0) >> 4) + 2;
            if (copy_len == 2)
            {
                if (src_off >= src_size) break;
                copy_len = src[src_off++] + 18;
            }
            for (uint i = 0; i < copy_len && dest_off < out_size; i++)
            {
                u8 val = (dist_back > dest_off) ? 0 : out[dest_off - dist_back];
                out[dest_off++] = val;
            }
        }
        code_word <<= 1;
        bits_left--;
    }
}

static void decompress_mpbin_rle(u8 *out, uint out_size, const u8 *src, uint src_size)
{
    uint dest_off = 0;
    uint src_off = 0;
    while (dest_off < out_size && src_off < src_size)
    {
        u8 code_byte = src[src_off++];
        uint rep_len = code_byte & 0x7F;
        if (code_byte & 0x80)
        {
            for (uint i = 0; i < rep_len && dest_off < out_size && src_off < src_size; i++)
                out[dest_off++] = src[src_off++];
        }
        else
        {
            if (src_off >= src_size) break;
            u8 rep_byte = src[src_off++];
            for (uint i = 0; i < rep_len && dest_off < out_size; i++)
                out[dest_off++] = rep_byte;
        }
    }
}

static void decompress_mpbin_inflate(u8 *out, uint out_size, const u8 *src, uint comp_size)
{
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.avail_in = comp_size;
    stream.next_in = (Bytef*)src;
    stream.avail_out = out_size;
    stream.next_out = (Bytef*)out;
    if (inflateInit2(&stream, 15 + 32) == Z_OK)
    {
        inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
    }
}

static enumError extract_mpbin_file ( ccp arg, ccp basedir, uint depth )
{
    ccp ext = strrchr(arg, '.');
    if ( !ext || strcasecmp(ext, ".bin") )
        return ERR_NOTHING_TO_DO;

    u8 *raw = 0;
    size_t raw_size = 0;
    enumError err = LoadFileAlloc(arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
    if (err) return err;
    if (raw_size < 8 || raw_size > UINT_MAX) { FREE(raw); return ERR_NOTHING_TO_DO; }

    u32 num_files = ((u32)raw[0] << 24) | ((u32)raw[1] << 16) | ((u32)raw[2] << 8) | raw[3];
    if (num_files < 1 || num_files > 4096 || 4 + 4*num_files > raw_size)
    {
        FREE(raw);
        return ERR_NOTHING_TO_DO;
    }

    u32 first_off = ((u32)raw[4] << 24) | ((u32)raw[5] << 16) | ((u32)raw[6] << 8) | raw[7];
    if (first_off < 4 + 4*num_files || first_off >= raw_size)
    {
        FREE(raw);
        return ERR_NOTHING_TO_DO;
    }

    for (uint i = 0; i < num_files; i++)
    {
        u32 off = ((u32)raw[4+4*i] << 24) | ((u32)raw[4+4*i+1] << 16) | ((u32)raw[4+4*i+2] << 8) | raw[4+4*i+3];
        if (off + 8 > raw_size)
        {
            FREE(raw);
            return ERR_NOTHING_TO_DO;
        }
        u32 ctype = ((u32)raw[off+4] << 24) | ((u32)raw[off+5] << 16) | ((u32)raw[off+6] << 8) | raw[off+7];
        if (ctype != 0 && ctype != 1 && ctype != 2 && ctype != 3 && ctype != 4 && ctype != 5 && ctype != 7)
        {
            FREE(raw);
            return ERR_NOTHING_TO_DO;
        }
    }

    char dest[PATH_MAX];
    beside_source_dest(dest, sizeof(dest), arg);
    if (verbose >= 0 || testmode)
        fprintf(stdlog, "%s%sEXTRACT MPBIN:%s (%u files) -> %s/\n",
            verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
            arg, num_files, dest);

    for (uint i = 0; !err && i < num_files; i++)
    {
        u32 off = ((u32)raw[4+4*i] << 24) | ((u32)raw[4+4*i+1] << 16) | ((u32)raw[4+4*i+2] << 8) | raw[4+4*i+3];
        u32 uncomp_size = ((u32)raw[off] << 24) | ((u32)raw[off+1] << 16) | ((u32)raw[off+2] << 8) | raw[off+3];
        u32 ctype = ((u32)raw[off+4] << 24) | ((u32)raw[off+5] << 16) | ((u32)raw[off+6] << 8) | raw[off+7];

        if (uncomp_size == 0) continue;
        if (uncomp_size > 256*1024*1024) { err = ERR_FILE_TOO_BIG; break; }

        if (testmode) continue;

        u8 *dest_buf = CALLOC(1, uncomp_size + 16);
        if (!dest_buf) { err = ENOMEM; break; }

        switch (ctype)
        {
            case 0:
                if (off + 8 + uncomp_size <= raw_size)
                    memcpy(dest_buf, raw + off + 8, uncomp_size);
                break;
            case 1:
                decompress_mpbin_lzss(dest_buf, uncomp_size, raw + off + 8, (uint)(raw_size - (off + 8)));
                break;
            case 2:
            case 3:
            case 4:
                if (off + 12 <= raw_size)
                    decompress_mpbin_slide(dest_buf, uncomp_size, raw + off + 12, (uint)(raw_size - (off + 12)));
                break;
            case 5:
                decompress_mpbin_rle(dest_buf, uncomp_size, raw + off + 8, (uint)(raw_size - (off + 8)));
                break;
            case 7:
                if (off + 16 <= raw_size)
                {
                    u32 comp_size = ((u32)raw[off+12] << 24) | ((u32)raw[off+13] << 16) | ((u32)raw[off+14] << 8) | raw[off+15];
                    decompress_mpbin_inflate(dest_buf, uncomp_size, raw + off + 16, comp_size);
                }
                break;
            default:
                break;
        }

        ccp sub_ext = "dat";
        if (uncomp_size >= 7 && !memcmp(dest_buf, "HSFV037", 7))
            sub_ext = "hsf";
        else if (uncomp_size >= 16 && dest_buf[12] == 0 && dest_buf[13] == 0 && dest_buf[14] == 0 && dest_buf[15] == 20)
            sub_ext = "atb";
        else if (uncomp_size >= 4 && !memcmp(dest_buf, "ARC\0", 4))
            sub_ext = "pac";
        else if (uncomp_size >= 4 && !memcmp(dest_buf, "darc", 4))
            sub_ext = "darc";
        else if (uncomp_size >= 4 && !memcmp(dest_buf, "SARC", 4))
            sub_ext = "sarc";

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%sfile%03u.%s", dest, basedir ? basedir : "", i, sub_ext);
        File_t F;
        err = CreateFileOpt(&F, true, path, false, arg);
        if (F.f && fwrite(dest_buf, 1, uncomp_size, F.f) != uncomp_size)
            err = FILEERROR1(&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", uncomp_size, path);
        ResetFile(&F, opt_preserve);
        FREE(dest_buf);
    }

    FREE(raw);
    if (!err && !testmode)
    {
        enumError sub_err = extract_tree_complete(dest, depth+1);
        if (err < sub_err) err = sub_err;
    }
    return err;
}

// Export the structural half of a Switch ("NX") BFRES container as XML.
// Wii U BFRES (ParseBFRES() in lib-bfres.c) is big-endian, version 3.x, and
// self-relative offsets; Switch reuses the "FRES" magic but is little-endian,
// version 9+, and every offset is *absolute* from the start of the file --
// a completely different, undocumented-in-tree layout that was reverse
// engineered against a real sample (~/Downloads/Male.bfres, a human
// character model) rather than guessed from the Wii U code:
//   0x08 u32 version, 0x0C u16 BOM (0xFEFF read LE = little endian),
//   0x28 s64 FMDL array offset, 0xDC u16 FMDL count,
//   FMDL: +0x08 name, +0x20 FVTX array, +0x28 FSHP array, +0x30 FSHP dict,
//         +0x38 FMAT array, +0x48 FMAT dict (undocumented -- found by
//         locating the dict's zero magic + entry count fields directly),
//   FSHP: +0x08 name, +0x10 *direct* FVTX pointer (no index indirection),
//   FVTX: +0x08 vertex attribute array, +0x10 attribute dict,
//         +0x4C attribute count, +0x4D buffer count,
//   attribute entry (16B): +0x00 name offset, +0x08 format u32,
//         +0x0C buffer offset u16, +0x0E buffer index u8.
// All of the above is verified against Male.bfres: FMDL name "TopL", 2
// shapes ("body__mt_body"/"body__mt_pants") each with a working direct FVTX
// pointer, 2 materials ("mt_body"/...), and attribute names "_p0"/"_n0"/
// "_i0" decoded correctly via the string table's u16-length-prefix
// convention. What is NOT verified or implemented: the actual vertex/index
// *data* location. The FVTX header's obvious "data offset" field (a u32 at
// +0x48) does not resolve to plausible geometry on the sample (neither as
// an absolute file offset nor added to the main header's buffer-pool-base
// field at 0x0A8) -- a brute-force scan across the whole file did locate a
// float region with a plausible human-scale Y/Z bounding box, but the X
// channel came back a constant near-zero denormal, meaning either the
// attribute's component packing or this exporter's own data-offset
// convention differs from the documented layout. Rather than guess and
// ship wrong geometry, this only exports the structure (names/materials/
// vertex-attribute layout) as XML -- the same "don't ship an unverified
// guess" rule this fork follows everywhere else. Geometry decode is a real,
// open gap; see PLAN.md.
static const char *rel_bfres_switch_string ( const u8 *d, size_t size, s64 off )
{
    if ( off < 2 || (size_t)off + 2 > size ) return NULL;
    const uint len = le16(d+off);
    if ( (size_t)off + 2 + len > size ) return NULL;
    return (const char*)(d+off+2);
}

static enumError extract_bfres_switch_manifest ( ccp arg )
{
    if ( !is_ext(arg,".bfres") && !is_ext(arg,".fres") )
	return ERR_NOTHING_TO_DO;

    u8 *d = 0;
    size_t size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&d,&size,0,0,0,false);
    if (err) return ERR_NOTHING_TO_DO;
    if ( size < 0xF0 || memcmp(d,"FRES",4) || le16(d+0x0C) != 0xFEFF )
	{ FREE(d); return ERR_NOTHING_TO_DO; }

    const s64 fmdl_arr = (s64)le64(d+0x28) ? (s64)le64(d+0x28) : (s64)le64(d+0x30);
    uint n_fmdl = le16(d+0xDC);
    if (!n_fmdl) n_fmdl = le16(d+0xC8);
    if (!n_fmdl && fmdl_arr > 0 && (size_t)fmdl_arr+4 <= size && !memcmp(d+fmdl_arr,"FMDL",4))
	n_fmdl = 1;
    if ( !n_fmdl || fmdl_arr < 0 || (size_t)fmdl_arr+0x50 > size || memcmp(d+fmdl_arr,"FMDL",4) )
	{ FREE(d); return ERR_NOTHING_TO_DO; }

    char dest[PATH_MAX];
    if (opt_dest)
	SubstDest(dest,sizeof(dest),arg,opt_dest,0,".xml",false);
    else
	snprintf(dest,sizeof(dest),"%s.xml",arg);
    if (verbose >= 0 || testmode)
	fprintf(stdlog,"%s%sEXTRACT BFRES(Switch) structure:%s -> %s\n",
	    verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
    if (testmode) { FREE(d); return ERR_OK; }

    File_t F;
    err = CreateFileOpt(&F,true,dest,false,arg);
    if (!F.f) { FREE(d); return err; }

    const s64 fmdl = fmdl_arr;
    const char *mname = rel_bfres_switch_string(d,size,(s64)le64(d+fmdl+0x08));
    fprintf(F.f,"<?xml version=\"1.0\"?>\n"
	"<!-- Switch BFRES: structure only -- see comment above "
	"extract_bfres_switch_manifest() in wszst.c for what is and isn't "
	"decoded. Vertex/index data offsets are not resolved yet. -->\n"
	"<bfres-switch name=\"%s\" fmdl-count=\"%u\">\n",
	mname ? mname : "", n_fmdl);

    const s64 ptr_30 = (s64)le64(d+fmdl+0x30);
    const bool is_v8 = (ptr_30 >= 0 && (size_t)ptr_30+4 <= size && !memcmp(d+ptr_30,"FSHP",4));

    const s64 fshp_arr = is_v8 ? (s64)le64(d+fmdl+0x30) : (s64)le64(d+fmdl+0x28);
    const s64 fshp_dict = is_v8 ? (s64)le64(d+fmdl+0x38) : (s64)le64(d+fmdl+0x30);
    const s64 fmat_arr = is_v8 ? (s64)le64(d+fmdl+0x40) : (s64)le64(d+fmdl+0x38);
    const s64 fmat_dict = is_v8 ? (s64)le64(d+fmdl+0x48) : (s64)le64(d+fmdl+0x48);
    uint n_fshp = fshp_dict >= 0 && (size_t)fshp_dict+8 <= size ? le32(d+fshp_dict+4) : 0;
    uint n_fmat = fmat_dict >= 0 && (size_t)fmat_dict+8 <= size ? le32(d+fmat_dict+4) : 0;
    if (!n_fshp && is_v8) n_fshp = le16(d+fmdl+0x68);
    if (!n_fmat && is_v8) n_fmat = le16(d+fmdl+0x6A);

    fprintf(F.f,"  <shapes count=\"%u\">\n",n_fshp);
    s64 sh = fshp_arr;
    for ( uint i = 0; i < n_fshp && sh >= 0 && (size_t)sh+0x10 <= size; i++ )
    {
	if (memcmp(d+sh,"FSHP",4)) break;
	const char *sname = rel_bfres_switch_string(d,size,(s64)le64(d+sh+0x08));
	const s64 fvtx = (s64)le64(d+sh+0x10);
	fprintf(F.f,"    <shape name=\"%s\">\n",sname ? sname : "");
	if ( fvtx >= 0 && (size_t)fvtx+0x50 <= size && !memcmp(d+fvtx,"FVTX",4) )
	{
	    const uint n_attr = d[fvtx+0x4C];
	    const s64 attr_arr = (s64)le64(d+fvtx+0x08);
	    for ( uint a = 0; a < n_attr; a++ )
	    {
		const s64 ae = attr_arr + (s64)a*16;
		if ( ae < 0 || (size_t)ae+16 > size ) break;
		const char *aname = rel_bfres_switch_string(d,size,(s64)le64(d+ae));
		fprintf(F.f,"      <attribute name=\"%s\" format=\"0x%x\" "
		    "buffer-offset=\"%u\" buffer-index=\"%u\"/>\n",
		    aname ? aname : "", le32(d+ae+8), le16(d+ae+12), d[ae+14]);
	    }
	}
	fprintf(F.f,"    </shape>\n");

	// FSHP entries aren't fixed-stride (each carries a variable-size
	// bounding/skin-index tail), so the next one is found the same way
	// Python's brute-force reference decode did: scan forward for the
	// next literal "FSHP" magic.
	const u8 *next = i+1 < n_fshp
	    ? memmem(d+sh+4,size-(sh+4),"FSHP",4) : NULL;
	sh = next ? next-d : -1;
    }
    fprintf(F.f,"  </shapes>\n");

    fprintf(F.f,"  <materials count=\"%u\">\n",n_fmat);
    if ( fmat_arr >= 0 && (size_t)fmat_arr+0x10 <= size && !memcmp(d+fmat_arr,"FMAT",4) )
    {
	const char *matname = rel_bfres_switch_string(d,size,(s64)le64(d+fmat_arr+0x08));
	fprintf(F.f,"    <material name=\"%s\"/>\n",matname ? matname : "");
    }
    fprintf(F.f,"  </materials>\n");
    fprintf(F.f,"</bfres-switch>\n");

    if (ferror(F.f) && !err) err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing XML failed: %s\n",dest);
    ResetFile(&F,opt_preserve);
    FREE(d);
    return err ? err : ERR_OK;
}

// Export the structural half of a 3DS/Wii U bitmap font (BCFNT/BFFNT) as
// XML. Wii's own font family (RFNT/RFNA, container magic "RFNT"/"RFNA") is
// already fully decoded to PNG via the TGLP branch in AssignIMG() --
// lib-image2.c. The 3DS/Wii U family reuses the exact same FINF/TGLP
// section shapes (confirmed field-for-field against hadashisora/NintyFont's
// from-source CFNT/FINF/TGLP reader for the container header and TGLP
// itself), but is NOT simply "BRFNT with a different container magic":
//   - 3DS uses magic "CFNT"; Wii U uses "FFNT" -- a real, different magic,
//     not a NintyFont naming quirk (NintyFont's "CFNT" reader is 3DS-only
//     and does not cover "FFNT" at all).
//   - The FINF pointer fields (ptrGlyph/ptrWidth/ptrMap) sit 4 bytes later
//     than NintyFont's declared struct says (FINF+0x14/+0x18/+0x1C, not
//     NintyFont's documented +0x10/+0x14/+0x18) -- caught by cross-checking
//     against two real retail Wii U .bffnt samples (DynaFont_NW_Demo.bffnt,
//     CafeStd_25.bffnt): NintyFont's offsets point at garbage, the +4
//     shifted ones land exactly on a real "TGLP"/"CWDH"/"CMAP" magic. This
//     fork's own decode uses the verified real offsets, not the reference
//     tool's.
//   - TGLP's sheetFormat is a 3DS/Cafe GPU texture-format id, a completely
//     different numbering from the Wii GX ids GetImageGeometry() (used by
//     the BRFNT decode path) already understands -- reusing that table
//     would silently decode the wrong pixel format. No 3DS/Cafe format
//     table exists in this fork yet, so pixel decode is left undone rather
//     than guessed; this only exports the verified structural fields
//     (cell/sheet geometry, sheet count/format id, pointers), the same
//     "don't ship an unverified guess" scope as the Switch BFRES manifest
//     above. No real .bcfnt sample was found to verify the 3DS side
//     specifically, but the container/TGLP shape is shared with .bffnt --
//     see PLAN.md.
static enumError extract_cfnt_manifest ( ccp arg )
{
    if ( !is_ext(arg,".bffnt") && !is_ext(arg,".bcfnt") && !is_ext(arg,".ffnt") && !is_ext(arg,".cfnt") )
	return ERR_NOTHING_TO_DO;

    u8 *d = 0;
    size_t size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&d,&size,0,0,0,false);
    if (err) return ERR_NOTHING_TO_DO;
    const bool is_cfnt = size >= 4 && !memcmp(d,"CFNT",4);
    const bool is_ffnt = size >= 4 && !memcmp(d,"FFNT",4);
    if ( (!is_cfnt && !is_ffnt) || size < 0x14 )
	{ FREE(d); return ERR_NOTHING_TO_DO; }

    const bool be = d[4]==0xFE && d[5]==0xFF; // BOM: FEFF=big, FFFE=little
    if ( !be && !(d[4]==0xFF && d[5]==0xFE) )
	{ FREE(d); return ERR_NOTHING_TO_DO; } // neither BOM reading is valid

#define CF16(p) ( be ? be16(p) : le16(p) )
#define CF32(p) ( be ? be32(p) : le32(p) )

    const uint header_size = CF16(d+6);
    if ( header_size < 0x14 || (size_t)header_size+0x14 > size || memcmp(d+header_size,"FINF",4) )
	{ FREE(d); return ERR_NOTHING_TO_DO; }
    const size_t finf = header_size;
    const uint finf_len = CF32(d+finf+4);
    if ( (finf_len != 0x1C && finf_len != 0x20) || finf+finf_len > size )
	{ FREE(d); return ERR_NOTHING_TO_DO; }
    const uint font_type = d[finf+8];

    char dest[PATH_MAX];
    if (opt_dest)
	SubstDest(dest,sizeof(dest),arg,opt_dest,0,".xml",false);
    else
	snprintf(dest,sizeof(dest),"%s.xml",arg);
    if (verbose >= 0 || testmode)
	fprintf(stdlog,"%s%sEXTRACT %s structure:%s -> %s\n",
	    verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
	    is_cfnt ? "BCFNT" : "BFFNT", arg, dest);
    if (testmode) { FREE(d); return ERR_OK; }

    File_t F;
    err = CreateFileOpt(&F,true,dest,false,arg);
    if (!F.f) { FREE(d); return err; }
    fprintf(F.f,"<?xml version=\"1.0\"?>\n"
	"<!-- %s: structure only -- see comment above extract_cfnt_manifest() "
	"in wszst.c for what is and isn't decoded. Sheet pixel data uses a "
	"3DS/Cafe GPU format id this fork doesn't have a table for yet. -->\n"
	"<%s font-type=\"%u\">\n", is_cfnt ? "BCFNT" : "BFFNT",
	is_cfnt ? "bcfnt" : "bffnt", font_type);

    if ( font_type == 1 && finf_len >= 0x20 && (size_t)finf+0x18 <= size )
    {
	const uint ptr_glyph = CF32(d+finf+0x14);
	const size_t tglp = (size_t)ptr_glyph - 8;
	if ( ptr_glyph >= 8 && tglp+0x20 <= size && !memcmp(d+tglp,"TGLP",4) )
	{
	    fprintf(F.f,
		"  <tglp cell-width=\"%u\" cell-height=\"%u\" baseline=\"%u\" "
		"max-char-width=\"%u\" sheet-size=\"%u\" sheet-count=\"%u\" "
		"sheet-format=\"0x%x\" cells-per-row=\"%u\" cells-per-column=\"%u\" "
		"sheet-width=\"%u\" sheet-height=\"%u\" sheet-data-offset=\"0x%x\"/>\n",
		d[tglp+8], d[tglp+9], d[tglp+10], d[tglp+11],
		CF32(d+tglp+0xC), CF16(d+tglp+0x10), CF16(d+tglp+0x12),
		CF16(d+tglp+0x14), CF16(d+tglp+0x16),
		CF16(d+tglp+0x18), CF16(d+tglp+0x1A), CF32(d+tglp+0x1C));
	}
	else
	    fprintf(F.f,"  <!-- fontType==1 (TGLP) but ptrGlyph-8 (0x%zx) "
		"isn't a valid TGLP -- truncated or unexpected file -->\n",tglp);
    }
    else
	fprintf(F.f,"  <!-- fontType==%u (bitmap/CGLP glyphs), not the TGLP "
	    "case this fork decodes -->\n",font_type);

    fprintf(F.f,"</%s>\n", is_cfnt ? "bcfnt" : "bffnt");
#undef CF16
#undef CF32

    if (ferror(F.f) && !err) err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing XML failed: %s\n",dest);
    ResetFile(&F,opt_preserve);
    FREE(d);
    return err ? err : ERR_OK;
}

// Decode a Wii bitmap font (BRFNT/BRFNA, container magic "RFNT"/"RFNA") to
// PNG sheet(s) during "wszst XX". The pixel decode itself already exists --
// AssignIMG()'s TGLP branch in lib-image2.c fully understands it (see the
// comment above extract_cfnt_manifest()) -- but nothing in the XX/EXTRACT
// tree walk ever called it; only an explicit `wimgt DECODE some.brfnt` did,
// so a real font found inside an extracted disc was left as an undecoded
// raw member. Mirrors wimgt.c's own cmd_decode(): TGLP glyph sheets are
// independent atlases, not a mip chain, so each info_n_image record gets
// its own PNG.
static enumError decode_brfnt_if_possible ( ccp arg )
{
    u8 magic[4];
    FILE *probe = fopen(arg,"rb");
    if (!probe) return ERR_NOT_EXISTS;
    const size_t n_magic = fread(magic,1,sizeof(magic),probe);
    fclose(probe);
    if ( n_magic != sizeof(magic) || ( memcmp(magic,"RFNT",4) && memcmp(magic,"RFNA",4) ) )
	return ERR_NOTHING_TO_DO;

    Image_t img;
    enumError max_err = LoadIMG(&img,true,arg,0,false,true,false);
    if (max_err) return max_err;

    char dest_base[PATH_MAX];
    if (opt_dest)
	SubstDest(dest_base,sizeof(dest_base),arg,opt_dest,0,".png",false);
    else
	snprintf(dest_base,sizeof(dest_base),"%s.png",arg);

    const uint record_images = img.info_fform == FF_UNKNOWN && img.info_n_image > 1
	? img.info_n_image : 1;

    if ( record_images <= 1 )
    {
	if (verbose >= 0 || testmode)
	    fprintf(stdlog,"%s%sDECODE BRFNT:%s -> PNG:%s\n",
		verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest_base);
	if (!testmode)
	    max_err = SavePNG(&img,true,0,dest_base,0,0,false,0);
	ResetIMG(&img);
	return max_err;
    }

    ResetIMG(&img);
    for ( uint image_index = 0; image_index < record_images; image_index++ )
    {
	const enumError load_err = LoadIMG(&img,true,arg,image_index,false,true,false);
	if (max_err < load_err) max_err = load_err;
	if (load_err) continue;

	char dest[PATH_MAX];
	ccp ext = strrchr(dest_base,'.');
	if (ext)
	    snprintf(dest,sizeof(dest),"%.*s.sheet%03u%s",
		(int)(ext-dest_base),dest_base,image_index,ext);
	else
	    snprintf(dest,sizeof(dest),"%s.sheet%03u.png",dest_base,image_index);

	if (verbose >= 0 || testmode)
	    fprintf(stdlog,"%s%sDECODE BRFNT:%s[%u] -> PNG:%s\n",
		verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, image_index, dest);
	if (!testmode)
	{
	    const enumError sheet_err = SavePNG(&img,false,0,dest,0,0,false,0);
	    if (max_err < sheet_err) max_err = sheet_err;
	}
	ResetIMG(&img);
    }
    return max_err;
}

// Convert a BFLYT/BCLYT/BRLYT/BRLAN layout or animation to text (.tflyt)
// during "wszst XX", mirroring what `wszst TEXT` already does for a single
// file (see cmd_convert()'s "Handle layout formats" branch a few hundred
// lines below). A layout/anim member found inside an extracted tree (e.g.
// a game's LZ-compressed romfs .bin -- already plain by the time this runs,
// decompress_nintendo_file() ran first) was previously left as an undecoded
// binary blob; nothing in the XX/EXTRACT tree walk called ScanBFLYT()/
// SaveTextBFLYT(). Only runs when export_count>0 (XX aliases to XEXPORT),
// matching the gate used by decode_brfnt_if_possible()/export_model_if_possible().
static enumError decode_bflyt_if_possible ( ccp arg )
{
    if (export_count <= 0) return ERR_NOTHING_TO_DO;

    raw_data_t raw;
    InitializeRawData(&raw);
    enumError err = LoadRawData(&raw,false,arg,0,false,0);
    if (err > ERR_WARNING) { ResetRawData(&raw); return ERR_NOTHING_TO_DO; }
    if ( raw.fform != FF_BFLYT && raw.fform != FF_BCLYT &&
	 raw.fform != FF_BRLYT && raw.fform != FF_BRLAN )
	{ ResetRawData(&raw); return ERR_NOTHING_TO_DO; }

    char dest[PATH_MAX];
    if (opt_dest)
	SubstDest(dest,sizeof(dest),arg,opt_dest,0,".tflyt",false);
    else
	snprintf(dest,sizeof(dest),"%s.tflyt",arg);
    if (verbose >= 0 || testmode)
	fprintf(stdlog,"%s%sCREATE/TEXT %s:%s -> %s\n",
	    verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
	    GetNameFF(raw.fform,0), arg, dest);
    if (testmode) { ResetRawData(&raw); return ERR_OK; }

    bflyt_t bflyt;
    InitializeBFLYT(&bflyt);
    err = ScanBFLYT(&bflyt,false,raw.data,raw.data_size);
    if (!err)
	err = SaveTextBFLYT(&bflyt,dest,true);
    ResetBFLYT(&bflyt);
    ResetRawData(&raw);
    return err ? err : ERR_OK;
}

static enumError decode_byml_if_possible ( ccp arg )
{
    if (export_count <= 0) return ERR_NOTHING_TO_DO;
    if ( !is_ext(arg,".byml") && !is_ext(arg,".byaml") && !is_ext(arg,".bin") )
	return ERR_NOTHING_TO_DO;

    u8 *data = 0;
    size_t size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&data,&size,0,0,0,false);
    if (err) return ERR_NOTHING_TO_DO;
    if (size < 16 || size > UINT_MAX) { FREE(data); return ERR_NOTHING_TO_DO; }

    const nfmt_type_t type = DetectNintendoFormat(data,size,arg).type;
    if (type != NFMT_BYML) { FREE(data); return ERR_NOTHING_TO_DO; }

    char dest[PATH_MAX];
    if (opt_dest)
        SubstDest(dest,sizeof(dest),arg,opt_dest,0,".yaml",false);
    else
        snprintf(dest,sizeof(dest),"%s.yaml",arg);

    if (verbose >= 0 || testmode)
        fprintf(stdlog,"%s%sDECODE BYML:%s -> YAML:%s\n",
            verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
    if (testmode) { FREE(data); return ERR_OK; }

    File_t F;
    err = CreateFileOpt(&F,true,dest,false,arg);
    if (!F.f) { FREE(data); return err; }
    err = DecodeBYML_YAML(F.f,data,size);
    ResetFile(&F,opt_preserve);
    FREE(data);
    return err;
}

// Export the structural half of a Nitro sprite set as XML.  NCGR/NCLR pixels
// remain ordinary wimgt inputs; the manifest records the exact OAM and frame
// values that connect those pixels to NCER/NANR cell/animation resources.
static enumError extract_nitro_sprite_manifest ( ccp arg )
{
    if ( !is_ext(arg,".bncr") && !is_ext(arg,".nanr") && !is_ext(arg,".ncer") && !is_ext(arg,".bin") )
	return ERR_NOTHING_TO_DO;

    u8 *data = 0;
    size_t file_size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&data,&file_size,0,0,0,false);
    if (err) return ERR_NOTHING_TO_DO;
    if (file_size > UINT_MAX) { FREE(data); return ERR_FILE_TOO_BIG; }
    const nfmt_type_t type = DetectNintendoFormat(data,file_size,arg).type;
    if (type != NFMT_NCER && type != NFMT_NANR)
        { FREE(data); return ERR_NOTHING_TO_DO; }

    char dest[PATH_MAX];
    if (opt_dest)
        SubstDest(dest,sizeof(dest),arg,opt_dest,0,".xml",false);
    else
        snprintf(dest,sizeof(dest),"%s.xml",arg);
    if (verbose >= 0 || testmode)
        fprintf(stdlog,"%s%sEXTRACT %s XML:%s -> %s\n",verbose > 0 ? "\n" : "",
            testmode ? "WOULD " : "",GetNintendoFormatName(type),arg,dest);
    if (testmode) { FREE(data); return ERR_OK; }

    File_t F;
    err = CreateFileOpt(&F,true,dest,false,arg);
    if (!F.f) { FREE(data); return err; }
    if (type == NFMT_NCER)
    {
        nintendo_ncer_t ncer;
        err = ScanNCER(&ncer,data,file_size);
        if (err) err = ERR_INVALID_DATA;
        if (!err) fprintf(F.f,"<?xml version=\"1.0\"?>\n<ncer cells=\"%u\">\n",ncer.n_cells);
        for (uint i = 0; !err && i < ncer.n_cells; i++)
        {
            uint n_obj; const u8 *oam;
            err = GetNCERCell(&ncer,i,&n_obj,&oam);
            if (!err) fprintf(F.f,"  <cell index=\"%u\" objects=\"%u\">\n",i,n_obj);
            for (uint j = 0; !err && j < n_obj; j++,oam += 6)
                if (fprintf(F.f,"    <obj attr0=\"0x%04x\" attr1=\"0x%04x\" attr2=\"0x%04x\"/>\n",
                    le16(oam),le16(oam+2),le16(oam+4)) < 0) err = ERR_WRITE_FAILED;
            if (!err && fprintf(F.f,"  </cell>\n") < 0) err = ERR_WRITE_FAILED;
        }
        if (!err && fprintf(F.f,"</ncer>\n") < 0) err = ERR_WRITE_FAILED;
    }
    else
    {
        nintendo_nanr_t nanr;
        err = ScanNANR(&nanr,data,file_size);
        if (err) err = ERR_INVALID_DATA;
        if (!err) fprintf(F.f,"<?xml version=\"1.0\"?>\n<nanr animations=\"%u\" frames=\"%u\">\n",
            nanr.n_animations,nanr.n_frames);
        for (uint i = 0; !err && i < nanr.n_animations; i++)
        {
            uint n_frames; const u8 *frames;
            err = GetNANRAnimation(&nanr,i,&n_frames,&frames);
            if (!err) fprintf(F.f,"  <animation index=\"%u\" frames=\"%u\">\n",i,n_frames);
            for (uint j = 0; !err && j < n_frames; j++,frames += 8)
            {
                const uint off = le32(frames);
                if (fprintf(F.f,"    <frame cell=\"%u\" duration=\"%u\" data-offset=\"0x%x\"/>\n",
                    le16(nanr.frame_data+off),le16(frames+4),off) < 0) err = ERR_WRITE_FAILED;
            }
            if (!err && fprintf(F.f,"  </animation>\n") < 0) err = ERR_WRITE_FAILED;
        }
        if (!err && fprintf(F.f,"</nanr>\n") < 0) err = ERR_WRITE_FAILED;
    }
    if (ferror(F.f) && !err) err = FILEERROR1(&F,ERR_WRITE_FAILED,"Writing XML failed: %s\n",dest);
    ResetFile(&F,opt_preserve);
    FREE(data);
    return err ? err : ERR_OK;
}

// Export a 3D model file (MDL0 from a BRRES, or a standalone NSBMD/BCRES/
// BCH/BFRES container) to DAE, mirroring what `wmdlt ENCODE -d out.dae`
// does standalone. XX/XEXPORT's extraction pipeline used to only decode
// BRRES down to its raw MDL0/texture/anim members -- the model geometry
// itself was written out but never converted, so a real disc's 3D models
// (e.g. Kirby's Epic Yarn's .gfa -> .brres -> 3DModels(NW4R)/*) never
// produced a .dae despite ParseMDL0()/ExportModelToDAE() already existing
// and being wired into wmdlt. Only runs when export_count>0 (XX aliases to
// XEXPORT, which sets it) so plain XDECODE/XALL keep their existing output.
static enumError export_model_if_possible ( ccp arg )
{
    if (export_count <= 0) return ERR_NOTHING_TO_DO;

    // A recursive post-pass visits a complete extracted game tree. Probe the
    // four-byte signature before loading a file so unrelated sounds, scripts,
    // and archives are not needlessly read into memory just to be declined.
    u8 magic[4];
    FILE *probe = fopen(arg,"rb");
    if (!probe) return ERR_NOT_EXISTS;
    const size_t n_magic = fread(magic,1,sizeof(magic),probe);
    fclose(probe);
    if ( n_magic != sizeof(magic)
        || memcmp(magic,"BMD0",4) && memcmp(magic,"CGFX",4)
        && memcmp(magic,"BCH\0",4) && memcmp(magic,"FRES",4)
        && memcmp(magic,"MDL0",4) )
        return ERR_NOTHING_TO_DO;

    u8 *data = 0;
    size_t size = 0;
    enumError err = LoadFileAlloc(arg,0,0,&data,&size,0,0,0,false);
    if (err) return ERR_NOTHING_TO_DO;

    model_t *model = 0;
    if ( size >= 4 && !memcmp(data,"BMD0",4) )
        model = ParseNSBMD(data,size);
    else if ( size >= 4 && !memcmp(data,"CGFX",4) )
        model = ParseBCRES(data,size);
    else if ( size >= 4 && !memcmp(data,"BCH\0",4) )
        model = (model_t*)ParseBCH(data,(uint)size);
    else if ( size >= 4 && !memcmp(data,"FRES",4) )
        model = ParseBFRES(data,size);
    else if ( size >= 4 && !memcmp(data,"MDL0",4) )
        model = ParseMDL0(data,size);
    FREE(data);
    if (!model) return ERR_NOTHING_TO_DO;
    char dest[PATH_MAX];
    if (opt_dest)
        SubstDest(dest,sizeof(dest),arg,opt_dest,0,".dae",false);
    else
        snprintf(dest,sizeof(dest),"%s.dae",arg);
    if (verbose >= 0 || testmode)
        fprintf(stdlog,"%s%sEXPORT MODEL:%s -> DAE:%s\n",
            verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
    if (!testmode)
        err = ExportModelToDAE(model,dest) ? ERROR0(ERR_WRITE_FAILED,
            "Failed to write DAE: %s\n",dest) : ERR_OK;
    FreeModel(model);
    return err;
}

// Convert a BRSAR (Wii sound archive) found in an extracted tree to MIDI+SF2.
// wszst itself does not link vgmtrans (it needs cmake+glib, which the
// Makefile deliberately keeps out of the main tool -- see NO_VGMTRANS), so
// this shells out to the sibling `wbrsar` binary that statically links it,
// the same pass-through style lib-passthru.c already uses for ctrtool/
// ndstool/sharpii/wit. Without this, "wszst XX" on a tree containing a
// .brsar silently left it as a raw, unconverted member. Only runs when
// export_count>0 (XX aliases to XEXPORT) so plain XDECODE/XALL are unaffected.
static enumError convert_brsar_if_possible ( ccp arg )
{
    if (export_count <= 0) return ERR_NOTHING_TO_DO;

    // Probe the four-byte signature before shelling out, same discipline as
    // export_model_if_possible() -- most files in a real tree are neither.
    u8 magic[4];
    FILE *probe = fopen(arg,"rb");
    if (!probe) return ERR_NOT_EXISTS;
    const size_t n_magic = fread(magic,1,sizeof(magic),probe);
    fclose(probe);
    if ( n_magic != sizeof(magic) || memcmp(magic,"RSAR",4) )
        return ERR_NOTHING_TO_DO;

    // Prefer the wbrsar built alongside this binary (same install/build dir)
    // over whatever a bare PATH search might turn up first.
    char tool[PATH_MAX] = {0};
    char mypath[PATH_MAX];
    GetProgramPath(mypath,sizeof(mypath),true,0);
    ccp slash = strrchr(mypath,'/');
    if (slash)
    {
        snprintf(tool,sizeof(tool),"%.*s/wbrsar",(int)(slash-mypath),mypath);
        if (access(tool,X_OK))
            *tool = 0;
    }
    if (!*tool)
    {
        ccp dirs = getenv("PATH");
        while ( dirs && *dirs && !*tool )
        {
            ccp end = strchr(dirs,':');
            const uint len = end ? (uint)(end-dirs) : (uint)strlen(dirs);
            if (len)
            {
                char cand[PATH_MAX];
                snprintf(cand,sizeof(cand),"%.*s/wbrsar",(int)len,dirs);
                if (!access(cand,X_OK))
                    snprintf(tool,sizeof(tool),"%s",cand);
            }
            dirs = end ? end+1 : 0;
        }
    }
    if (!*tool)
        return ERROR0(ERR_WARNING,
            "wbrsar not found; cannot convert BRSAR to MIDI+SF2: %s\n",arg);

    // Same "<stem>.d" staging convention every other extractor in this
    // codebase uses (SARC/PAC/GFA/pass-through containers).
    char dest[PATH_MAX];
    if (opt_dest)
        SubstDest(dest,sizeof(dest),arg,opt_dest,0,".d",false);
    else
        snprintf(dest,sizeof(dest),"%s.d",arg);
    if (verbose >= 0 || testmode)
        fprintf(stdlog,"%s%sEXPORT BRSAR:%s -> %s (wbrsar)\n",
            verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
    if (testmode) return ERR_OK;

    char * const child_argv[] = { tool, (char*)arg, dest, 0 };
    const pid_t pid = fork();
    if (pid < 0)
        return ERROR0(ERR_CANT_CREATE,"fork() failed converting BRSAR: %s\n",arg);
    if (pid == 0)
    {
        execv(tool,child_argv);
        _Exit(127); // execv failed
    }
    int status = 0;
    while ( waitpid(pid,&status,0) < 0 && errno == EINTR )
        ;
    if ( !WIFEXITED(status) || WEXITSTATUS(status) != 0 )
        return ERROR0(ERR_WARNING,"wbrsar failed for %s\n",arg);
    return ERR_OK;
}

// Walk an extracted BRRES/SZS tree looking for 3D model members (MDL0 and
// friends) to export as DAE, and for BRSAR sound archives to convert to
// MIDI+SF2. ExtractFilesSZS() writes both kinds of member out as plain
// binary -- it has no notion of DAE/MIDI export -- so this is the piece
// that actually turns on "XX exports 3D models and sound banks found inside
// .brres/.szs trees" rather than just decoding them down to raw members.
static enumError export_models_tree ( ccp root, uint depth )
{
    if (depth > 32) return ERR_FILE_TOO_BIG;
    DIR *dir = opendir(root);
    if (!dir) return ERR_NOT_EXISTS;
    enumError max_err = ERR_OK;
    struct dirent *de;
    while ((de = readdir(dir)))
    {
        if (!strcmp(de->d_name,".") || !strcmp(de->d_name,"..")) continue;
        char path[PATH_MAX];
        const int len = snprintf(path,sizeof(path),"%s/%s",root,de->d_name);
        if (len < 0 || (uint)len >= sizeof(path)) { max_err = ERR_FILE_TOO_BIG; continue; }
        struct stat st;
        if (lstat(path,&st)) { if (max_err < ERR_NOT_EXISTS) max_err = ERR_NOT_EXISTS; continue; }
        enumError err = ERR_OK;
        if (S_ISDIR(st.st_mode))
            err = export_models_tree(path,depth+1);
        else if (S_ISREG(st.st_mode))
        {
            // decode_brfnt_if_possible()/decode_bflyt_if_possible() are also
            // called from extract_one_file(), but that call is a no-op for
            // anything reached through a nested SARC/PAC/GFA/DARC container:
            // extract_tree_complete() zeroes export_count for the duration of
            // its inner extract_tree() walk (see the comment there), and both
            // functions gate on export_count>0 the same way export_model_if_
            // possible() does. This deferred, complete-tree pass is where
            // export_count is genuinely restored, so it's the one that
            // actually reaches layout/font members bundled inside those
            // containers -- confirmed necessary on a real retail 3DS disc,
            // where DARC-bundled layout families (see extract_darc_file())
            // are exactly this case: 0 conversions without this, real output
            // with it.
            err = decode_brfnt_if_possible(path);
            const enumError bflyt_err = decode_bflyt_if_possible(path);
            if (bflyt_err != ERR_NOTHING_TO_DO && err < bflyt_err) err = bflyt_err;
            const enumError byml_err = decode_byml_if_possible(path);
            if (byml_err != ERR_NOTHING_TO_DO && err < byml_err) err = byml_err;
            const enumError img_err = decode_image_if_possible(path);
            if (img_err != ERR_NOTHING_TO_DO && err < img_err) err = img_err;
            const enumError model_err = export_model_if_possible(path);
            if (model_err != ERR_NOTHING_TO_DO && err < model_err) err = model_err;
            const enumError brsar_err = convert_brsar_if_possible(path);
            if (brsar_err != ERR_NOTHING_TO_DO && err < brsar_err) err = brsar_err;
        }
        if (err != ERR_NOTHING_TO_DO && max_err < err) max_err = err;
    }
    closedir(dir);
    return max_err;
}

// Finish every extraction below ROOT before exporting any model. A staged
// disc/container can keep a model in one BRRES and its TEX0 in a later sibling
// archive; exporting while extract_tree() is still walking makes COLLADA image
// resolution depend on readdir/archive order. This wrapper is deliberately
// re-entrant: an outer two-pass run sets export_count to zero, so nested
// SARC/PAC/GFA/pass-through containers only decode and the outermost root owns
// the one final texture-index/export pass.
static enumError extract_tree_complete ( ccp root, uint depth )
{
    if (export_count <= 0)
        return extract_tree(root,depth);

    const int saved_export_count = export_count;
    export_count = 0;
    enumError err = extract_tree(root,depth);
    export_count = saved_export_count;

    ccp saved_dest = opt_dest;
    opt_dest = 0; // preserve each model's path; a shared --dest would collide
    SetDAETextureSearchRoot(root);
    const enumError model_err = export_models_tree(root,depth);
    SetDAETextureSearchRoot(0);
    opt_dest = saved_dest;
    if (err < model_err) err = model_err;
    return err;
}

// Decode raw Nintendo streams found below an extracted archive.  Sources stay
// in place and their decoded payload is written beside them, which preserves
// the project tree for deterministic archive rebuilds.
static enumError auto_decompress_tree ( ccp root, uint depth )
{
    if (depth > 32) return ERR_FILE_TOO_BIG;
    DIR *dir = opendir(root);
    if (!dir) return ERR_NOT_EXISTS;
    enumError max_err = ERR_OK;
    struct dirent *de;
    while ((de = readdir(dir)))
    {
        if (!strcmp(de->d_name,".") || !strcmp(de->d_name,"..")) continue;
        char path[PATH_MAX];
        const int len = snprintf(path,sizeof(path),"%s/%s",root,de->d_name);
        if (len < 0 || (uint)len >= sizeof(path)) { max_err = ERR_FILE_TOO_BIG; continue; }
        struct stat st;
        if (lstat(path,&st)) { if (max_err < ERR_NOT_EXISTS) max_err = ERR_NOT_EXISTS; continue; }
        enumError err = ERR_OK;
        if (S_ISDIR(st.st_mode))
            err = auto_decompress_tree(path,depth+1);
        else if (S_ISREG(st.st_mode))
            err = decompress_nintendo_file(path);
        if (err != ERR_NOTHING_TO_DO && max_err < err) max_err = err;
    }
    closedir(dir);
    return max_err;
}

static void get_extract_dest ( char *dest, uint dest_size, const szs_file_t *szs )
{
    ccp pattern = opt_dest;
    if (!pattern)
        switch (szs->fform_arch)
        {
            case FF_U8: case FF_WU8: case FF_LTA: case FF_LFL:
            case FF_RARC: case FF_PACK: case FF_RKC: pattern = "\1P/\1N.d/"; break;
            default: pattern = "\1P/\1F.d/"; break;
        }
    SubstDest(dest,dest_size,szs->fname,pattern,0,0,true);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		commands WC24DECRYPT/WC24ENCRYPT/BMS	///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_wc24 ( bool encrypt )
{
    if ( n_param < 3 )
	return ERROR0(ERR_SYNTAX,
	    encrypt
		? "Command WC24ENCRYPT needs: source dest key rsa-key [iv]\n"
		: "Command WC24DECRYPT needs: source dest key\n" );

    ParamList_t *param = first_param;
    ccp source = param->arg; param = param->next;
    ccp dest   = param->arg; param = param->next;
    ccp key    = param->arg; param = param->next;

    if (!encrypt)
	return WC24DecryptFile(source,dest,key);

    if (!param)
	return ERROR0(ERR_SYNTAX,"Command WC24ENCRYPT needs an RSA private key PEM\n");
    ccp rsa = param->arg; param = param->next;
    ccp iv  = param ? param->arg : 0;
    return WC24EncryptFile(source,dest,key,rsa,iv);
}

static enumError cmd_bms ( void )
{
    if ( n_param < 3 )
	return ERROR0(ERR_SYNTAX,"Command BMS needs: script source dest-dir\n");

    ParamList_t *param = first_param;
    ccp script = param->arg; param = param->next;
    ccp source = param->arg; param = param->next;
    ccp outdir = param->arg;
    return RunBmsScript(script,source,outdir);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    command SPRITES			///////////////
///////////////////////////////////////////////////////////////////////////////

// Loads a whole file, returning NULL on any failure.
static u8 * sprite_load ( ccp path, uint *size )
{
    u8 *data = 0;
    size_t fsize = 0;
    if ( LoadFileAlloc(path,0,0,&data,&fsize,0,2,0,false) || fsize > UINT_MAX )
    {
	FREE(data);
	return 0;
    }
    *size = (uint)fsize;
    return data;
}

// Writes a tightly packed RGBA8 buffer as a PNG through the image layer.
static enumError sprite_save_png
	( ccp path, u8 *rgba, uint w, uint h )
{
    Image_t img;
    InitializeIMG(&img);
    // AssignIMG's decoders own their pixels; hand over a copy so the caller
    // keeps control of its own buffer.
    const uint xw = EXPAND8(w), xh = EXPAND8(h);
    u8 *data = CALLOC(1,(size_t)xw*xh*4);
    if (!data) return ERR_CANT_CREATE;
    for ( uint y = 0; y < h; y++ )
	memcpy(data+(size_t)y*xw*4,rgba+(size_t)y*w*4,(size_t)w*4);
    img.data = data;
    img.data_alloced = true;
    img.data_size = xw*xh*4;
    img.width = w; img.xwidth = xw;
    img.height = h; img.xheight = xh;
    img.iform = img.info_iform = IMG_X_RGB;
    img.info_fform = FF_UNKNOWN;
    img.info_n_image = 1;
    img.alpha_status = 0;
    img.endian = &le_func;
    img.path = path;
    const enumError err = SaveIMG(&img,FF_PNG,0,0,path,true);
    ResetIMG(&img);
    return err;
}

// Given one file from a Nitro sprite set, find its siblings by base name and
// render whatever the available combination supports.  This is the
// "autoguess" front end: point it at a folder (or any member) and it works
// out which files belong together and what the right export is.
static enumError sprites_from_base ( ccp dir, ccp base )
{
    char path[PATH_MAX];
    uint ncgr_size = 0, nclr_size = 0, ncer_size = 0, nanr_size = 0;
    u8 *ncgr_data = 0, *nclr_data = 0, *ncer_data = 0, *nanr_data = 0;

    snprintf(path,sizeof(path),"%s%s.ncgr",dir,base);
    ncgr_data = sprite_load(path,&ncgr_size);
    snprintf(path,sizeof(path),"%s%s.nclr",dir,base);
    nclr_data = sprite_load(path,&nclr_size);
    snprintf(path,sizeof(path),"%s%s.ncer",dir,base);
    ncer_data = sprite_load(path,&ncer_size);
    snprintf(path,sizeof(path),"%s%s.nanr",dir,base);
    nanr_data = sprite_load(path,&nanr_size);

    enumError err = ERR_OK;
    if ( !ncgr_data || !nclr_data )
    {
	// Not a usable set; a lone NCGR still decodes as a tile sheet through
	// the normal wimgt path, so this is not an error.
	err = ERR_NOTHING_TO_DO;
	goto done;
    }

    nitro_ncgr_t ncgr;
    nitro_nclr_t nclr;
    if ( ScanNitroNCGR(&ncgr,ncgr_data,ncgr_size) )
    {
	err = ERROR0(ERR_INVALID_DATA,"Invalid NCGR: %s%s.ncgr\n",dir,base);
	goto done;
    }
    if ( ScanNitroNCLR(&nclr,nclr_data,nclr_size) )
    {
	err = ERROR0(ERR_INVALID_DATA,"Invalid NCLR: %s%s.nclr\n",dir,base);
	goto done;
    }

    if (!ncer_data)
    {
	// NCGR + NCLR only: the paletted tile sheet is what wimgt already
	// produces, so there is nothing extra to composite here.
	if ( verbose >= 0 )
	    fprintf(stdlog,"SPRITES %s: NCGR+NCLR only,"
		" use 'wimgt DECODE %s%s.ncgr' for the tile sheet\n",
		base,dir,base);
	ResetNitroNCLR(&nclr);
	err = ERR_OK;
	goto done;
    }

    nintendo_ncer_t ncer;
    if ( ScanNCER(&ncer,ncer_data,ncer_size) )
    {
	ResetNitroNCLR(&nclr);
	err = ERROR0(ERR_INVALID_DATA,"Invalid NCER: %s%s.ncer\n",dir,base);
	goto done;
    }

    char destdir[PATH_MAX];
    if ( opt_dest && *opt_dest )
	snprintf(destdir,sizeof(destdir),"%s",opt_dest);
    else
	snprintf(destdir,sizeof(destdir),"%s%s.d",dir,base);

    if ( verbose >= 0 || testmode )
	fprintf(stdlog,"%sSPRITES %s: %u cell%s, %u tiles @%ubpp, %u colors -> %s/\n",
	    testmode ? "WOULD " : "", base, ncer.n_cells,
	    ncer.n_cells == 1 ? "" : "s",
	    ncgr.n_tiles, ncgr.bpp, nclr.n_entries, destdir );

    uint written = 0;
    for ( uint i = 0; !err && i < ncer.n_cells; i++ )
    {
	u8 *rgba = 0;
	uint w = 0, h = 0;
	int ox = 0, oy = 0;
	if ( RenderNCERCell(&rgba,&w,&h,&ox,&oy,&ncer,i,&ncgr,&nclr) )
	    continue; // empty or unrenderable cell
	if (!testmode)
	{
	    snprintf(path,sizeof(path),"%s/cell_%03u.png",destdir,i);
	    err = sprite_save_png(path,rgba,w,h);
	}
	FREE(rgba);
	written++;
    }

    // NANR: emit one image per animation frame by reusing the cell renders.
    if ( !err && nanr_data )
    {
	nintendo_nanr_t nanr;
	if ( !ScanNANR(&nanr,nanr_data,nanr_size) )
	{
	    if ( verbose >= 0 || testmode )
		fprintf(stdlog,"SPRITES %s: %u animation%s\n",
		    base,nanr.n_animations,nanr.n_animations==1?"":"s");
	    for ( uint a = 0; !err && a < nanr.n_animations; a++ )
	    {
		uint n_frames = 0;
		const u8 *frames = 0;
		if ( GetNANRAnimation(&nanr,a,&n_frames,&frames) )
		    continue;
		for ( uint f = 0; !err && f < n_frames; f++ )
		{
		    // Each frame record starts with the cell index it shows.
		    const uint cell = (uint)frames[f*8] | (uint)frames[f*8+1]<<8;
		    if ( cell >= ncer.n_cells )
			continue;
		    u8 *rgba = 0;
		    uint w = 0, h = 0;
		    int ox = 0, oy = 0;
		    if ( RenderNCERCell(&rgba,&w,&h,&ox,&oy,&ncer,cell,&ncgr,&nclr) )
			continue;
		    if (!testmode)
		    {
			snprintf(path,sizeof(path),"%s/anim%02u_frame%03u.png",
				destdir,a,f);
			err = sprite_save_png(path,rgba,w,h);
		    }
		    FREE(rgba);
		    written++;
		}
	    }
	}
    }

    if ( verbose >= 0 )
	fprintf(stdlog,"SPRITES %s: %u image%s written\n",
		base,written,written==1?"":"s");

    ResetNitroNCLR(&nclr);

 done:
    FREE(ncgr_data); FREE(nclr_data); FREE(ncer_data); FREE(nanr_data);
    return err;
}

static enumError decode_image_if_possible ( ccp arg )
{
    if (export_count <= 0) return ERR_NOTHING_TO_DO;

    // Fast signature/extension probe before loading into memory
    u8 head[0x40];
    FILE *probe = fopen(arg,"rb");
    if (!probe) return ERR_NOT_EXISTS;
    const size_t n_head = fread(head,1,sizeof(head),probe);
    fclose(probe);
    if (n_head < 4) return ERR_NOTHING_TO_DO;

    // Don't decode already decoded PNGs, XMLs, YAMLs, etc.
    if ( !memcmp(head,"\x89PNG",4) || is_ext(arg,".png") || is_ext(arg,".xml") || is_ext(arg,".yaml") || is_ext(arg,".tflyt") || is_ext(arg,".dae") )
        return ERR_NOTHING_TO_DO;

    bool is_image = false;
    const file_format_t fform = GetByMagicFF(head,(uint)n_head,0);
    const nfmt_info_t nfmt = DetectNintendoFormat(head,(uint)n_head,arg);

    if ( !memcmp(head,"RGCN",4) || !memcmp(head,"NCGR",4) || is_ext(arg,".ncgr") )
        is_image = true;
    else if ( !memcmp(head,"RLCN",4) || !memcmp(head,"NCLR",4) || is_ext(arg,".nclr") )
        is_image = true;
    else if ( !memcmp(head,"RECN",4) || !memcmp(head,"NCER",4) || is_ext(arg,".ncer") )
        is_image = true;
    else if ( !memcmp(head,"BNTX",4) || is_ext(arg,".bntx") )
        is_image = true;
    else if ( !memcmp(head,"CTPK",4) || is_ext(arg,".ctpk") )
        is_image = true;
    else if ( !memcmp(head,"AJPG",4) || is_ext(arg,".ajpg") || is_ext(arg,".odh") )
        is_image = true;
    else if ( !memcmp(head,"TXTR",4) || is_ext(arg,".dsb") )
        is_image = true;
    else if ( nfmt.type == NFMT_BFLIM || nfmt.type == NFMT_BCLIM || is_ext(arg,".bflim") || is_ext(arg,".bclim") )
        is_image = true;
    else if ( fform == FF_TPL || is_ext(arg,".tpl") )
        is_image = true;
    else if ( fform == FF_TEX || is_ext(arg,".tex0") )
        is_image = true;

    if (!is_image)
        return ERR_NOTHING_TO_DO;

    // If it's an NCER, sprites_from_base composites full cells
    if ( !memcmp(head,"RECN",4) || !memcmp(head,"NCER",4) || is_ext(arg,".ncer") )
    {
        char dir[PATH_MAX], base[PATH_MAX];
        ccp slash = strrchr(arg,'/');
        if (slash) {
            snprintf(dir,sizeof(dir),"%.*s/",(int)(slash-arg),arg);
            snprintf(base,sizeof(base),"%s",slash+1);
        } else {
            snprintf(dir,sizeof(dir),"./");
            snprintf(base,sizeof(base),"%s",arg);
        }
        char *dot = strrchr(base,'.');
        if (dot) *dot = 0;
        return sprites_from_base(dir,base);
    }

    Image_t img;
    enumError err = LoadIMG(&img,true,arg,0,false,true,false);
    if (err) return ERR_NOTHING_TO_DO;
    if (!img.data) { ResetIMG(&img); return ERR_NOTHING_TO_DO; }

    char dest[PATH_MAX];
    if (opt_dest)
        SubstDest(dest,sizeof(dest),arg,opt_dest,0,".png",false);
    else
        snprintf(dest,sizeof(dest),"%s.png",arg);

    const uint record_images = img.info_n_image > 1 ? img.info_n_image : 1;
    if ( record_images <= 1 )
    {
        if (verbose >= 0 || testmode)
            fprintf(stdlog,"%s%sDECODE %s:%s -> PNG:%s\n",
                verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
                GetNameFF(img.info_fform,0), arg, dest);
        if (!testmode)
        {
            Transform2XIMG(&img);
            err = SavePNG(&img,false,0,dest,0,0,opt_overwrite>0,0);
        }
        ResetIMG(&img);
        return err ? err : ERR_OK;
    }

    ResetIMG(&img);
    char dir_dest[PATH_MAX];
    snprintf(dir_dest,sizeof(dir_dest),"%s.d",arg);
    for ( uint image_index = 0; image_index < record_images; image_index++ )
    {
        const enumError load_err = LoadIMG(&img,true,arg,image_index,false,true,false);
        if (load_err) continue;
        char sub_dest[PATH_MAX];
        snprintf(sub_dest,sizeof(sub_dest),"%s/image_%03u.png",dir_dest,image_index);
        if (verbose >= 0 || testmode)
            fprintf(stdlog,"%s%sDECODE %s:%s[%u] -> PNG:%s\n",
                verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
                GetNameFF(img.info_fform,0), arg, image_index, sub_dest);
        if (!testmode)
        {
            Transform2XIMG(&img);
            SavePNG(&img,false,0,sub_dest,0,0,opt_overwrite>0,0);
        }
        ResetIMG(&img);
    }
    return ERR_OK;
}

static enumError cmd_sprites ( void )
{
    if (!n_param)
	return ERROR0(ERR_SYNTAX,"Command SPRITES needs a directory or file\n");

    opt_mkdir = true;
    enumError max_err = ERR_OK;

    for ( ParamList_t *param = first_param; param; param = param->next )
    {
	ccp arg = param->arg;
	struct stat st;
	if ( stat(arg,&st) )
	{
	    if ( max_err < ERR_CANT_OPEN )
		max_err = ERROR0(ERR_CANT_OPEN,"Can't access: %s\n",arg);
	    continue;
	}

	if ( S_ISDIR(st.st_mode) )
	{
	    // Collect the distinct base names of every Nitro graphic file in
	    // the directory, then render each set once.
	    DIR *d = opendir(arg);
	    if (!d)
	    {
		max_err = ERROR0(ERR_CANT_OPEN,"Can't read directory: %s\n",arg);
		continue;
	    }
	    StringField_t bases = {0};
	    InitializeStringField(&bases);
	    struct dirent *e;
	    while ( ( e = readdir(d) ) != 0 )
	    {
		ccp dot = strrchr(e->d_name,'.');
		if ( !dot || ( strcasecmp(dot,".ncgr") && strcasecmp(dot,".ncer")
			    && strcasecmp(dot,".nclr") && strcasecmp(dot,".nanr") ) )
		    continue;
		char base[PATH_MAX];
		const uint len = (uint)(dot-e->d_name);
		if ( len >= sizeof(base) ) continue;
		memcpy(base,e->d_name,len);
		base[len] = 0;
		InsertStringField(&bases,base,false);
	    }
	    closedir(d);

	    char dir[PATH_MAX];
	    const uint n = strlen(arg);
	    snprintf(dir,sizeof(dir),"%s%s",arg, n && arg[n-1]=='/' ? "" : "/");
	    for ( int i = 0; i < bases.used; i++ )
	    {
		const enumError err = sprites_from_base(dir,bases.field[i]);
		if ( err != ERR_NOTHING_TO_DO && max_err < err )
		    max_err = err;
	    }
	    ResetStringField(&bases);
	}
	else
	{
	    // A single member file: derive the base name and use its directory.
	    char dir[PATH_MAX], base[PATH_MAX];
	    ccp slash = strrchr(arg,'/');
	    ccp fname = slash ? slash+1 : arg;
	    const uint dlen = slash ? (uint)(slash-arg)+1 : 0;
	    if ( dlen >= sizeof(dir) ) continue;
	    memcpy(dir,arg,dlen);
	    dir[dlen] = 0;
	    ccp dot = strrchr(fname,'.');
	    const uint blen = dot ? (uint)(dot-fname) : strlen(fname);
	    if ( blen >= sizeof(base) ) continue;
	    memcpy(base,fname,blen);
	    base[blen] = 0;

	    const enumError err = sprites_from_base(dir,base);
	    if ( err == ERR_NOTHING_TO_DO )
		ERROR0(ERR_WARNING,"No complete Nitro sprite set for: %s\n",arg);
	    else if ( max_err < err )
		max_err = err;
	}
    }
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    command BCH				///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_bch ( void )
{
    if (!n_param)
	return ERROR0(ERR_SYNTAX,"Command BCH needs a file\n");

    enumError max_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];
	u8 *data = 0;
	size_t fsize = 0;
	enumError err = LoadFileAlloc(arg,0,0,&data,&fsize,0,0,0,false);
	if (err) { if (max_err<err) max_err = err; continue; }

	// CGFX/BCRES is the other 3DS graphics container; list it here too so
	// one command covers both rather than making the user pick.
	if ( fsize >= 4 && !memcmp(data,"CGFX",4) )
	{
	    cgfx_t cgfx;
	    if ( !ScanCGFX(&cgfx,data,(size_t)fsize) )
	    {
		FREE(data);
		ERROR0(ERR_INVALID_DATA,"Invalid CGFX file: %s\n",arg);
		if (max_err<ERR_INVALID_DATA) max_err = ERR_INVALID_DATA;
		continue;
	    }
	    printf("\n%s%s%s\n",colout->heading,arg,colout->reset);
	    printf("  CGFX/BCRES, revision 0x%08x\n",cgfx.revision);
	    uint tot = 0;
	    for ( int i = 0; i < CGFX_N_DICTS; i++ )
	    {
		if (!cgfx.dict[i].n) continue;
		tot += cgfx.dict[i].n;
		printf("  %-21s %u\n",GetCGFXDictName(i),cgfx.dict[i].n);
		for ( uint k = 0; k < cgfx.dict[i].n; k++ )
		    printf("      @%08x  %s\n",
			cgfx.dict[i].entries[k].address,cgfx.dict[i].entries[k].name);
	    }
	    if (!tot) printf("  (no named content)\n");
	    ResetCGFX(&cgfx);
	    FREE(data);
	    continue;
	}

	bch_t bch;
	err = ScanBCH(&bch,data,(uint)fsize);
	FREE(data);
	if (err)
	{
	    ERROR0(ERR_INVALID_DATA,"Not a BCH or CGFX file: %s\n",arg);
	    if (max_err<ERR_INVALID_DATA) max_err = ERR_INVALID_DATA;
	    continue;
	}

	printf("\n%s%s%s\n",colout->heading,arg,colout->reset);
	printf("  version: backward 0x%02x, forward 0x%02x\n",bch.bc,bch.fc);
	printf("  sections: contents 0x%x+0x%x, strings 0x%x+0x%x,"
	       " commands 0x%x+0x%x, raw 0x%x+0x%x, reloc 0x%x+0x%x\n",
	    bch.contents_addr,bch.contents_len, bch.strings_addr,bch.strings_len,
	    bch.commands_addr,bch.commands_len, bch.raw_data_addr,bch.raw_data_len,
	    bch.reloc_addr,bch.reloc_len);

	uint total = 0;
	for ( int i = 0; i < BCH_N_DICTS; i++ )
	{
	    const bch_dict_t *d = bch.dict+i;
	    if (!d->n) continue;
	    total += d->n;
	    printf("  %-21s %u\n",GetBCHDictName(i),d->n);
	    for ( uint k = 0; k < d->n; k++ )
		printf("      @%08x  %s\n",d->entries[k].address,d->entries[k].name);
	}
	if (!total)
	    printf("  (no named content)\n");

	ResetBCH(&bch);
    }

    ResetStringField(&plist);
    return max_err;
}

// Process a single file with the full XX pipeline: external pass-through
// (strong, then weak) first, then the native Nintendo codecs, and finally
// the regular SZS/U8/BRRES extractor.  When a pass-through tool unpacks a
// container into a staging directory, the staged tree is walked recursively
// so every leaf is peeled in turn.  Returns the strongest error seen, or
// ERR_NOTHING_TO_DO when nothing claimed the file.
static enumError extract_one_file ( ccp arg, ccp basedir, uint depth )
{
    // Pass-through staging honours --dest like the native extractors do: the
    // unpacked tree lands as <dest>/<stem>.d (or beside the source when no
    // destination was given).  Normalise the base so the stage joins without
    // a leading-slash bug when --dest lacks a trailing separator.
    char pbase_buf[PATH_MAX] = "";
    ccp pbase = opt_dest && *opt_dest ? opt_dest : basedir;
    if (pbase)
    {
	snprintf(pbase_buf,sizeof(pbase_buf),"%s",pbase);
	const uint n = strlen(pbase_buf);
	if (n && pbase_buf[n-1] != '/' && n+1 < sizeof(pbase_buf))
	{
	    pbase_buf[n] = '/';
	    pbase_buf[n+1] = 0;
	}
	pbase = pbase_buf;
    }

    // This manifest is emitted by every native archive extraction. It is an
    // input to CREATE, not another container to peel during recursive XX.
    ccp arg_name = strrchr(arg,'/');
    arg_name = arg_name ? arg_name+1 : arg;
    if (!strcmp(arg_name,"wszst-setup.txt"))
	return ERR_OK;
    if ( depth && ( !strncmp(arg_name,"image.",6)
		    || !strncmp(arg_name,"mipmap-",7) ))
	return ERR_OK;

    // Containers the main tools cannot open natively are passed through
    // to external unpackers (wit, ndstool, ctrtool, sharpii).  The strong
    // variant runs FIRST, claimed by header signature only: it neither
    // reads the whole file (so disc images above --max-file-size reach
    // wit instead of failing the native probes) nor steals any file the
    // native decoders can handle (extension-only claims are left below).
    if (!opt_no_passthrough)
    {
	char staged_dir[PATH_MAX] = "";
	enumError pas_err = PassthruExtractStrong(arg,pbase,staged_dir,sizeof(staged_dir));
	if (pas_err != ERR_NOTHING_TO_DO)
	    return pas_err == ERR_OK && *staged_dir
		? extract_tree_complete(staged_dir,depth+1)
		: pas_err;
    }

    enumError err = extract_sarc_file(arg,basedir,depth);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = extract_gfa_file(arg,basedir,depth);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = extract_pac_file(arg,basedir,depth);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = extract_darc_file(arg,basedir,depth);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = extract_narc_file(arg,basedir,depth);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = extract_mpbin_file(arg,basedir,depth);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = extract_at7_file(arg,basedir,depth);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = extract_ctpk_file(arg,basedir,depth);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = extract_nitro_sprite_manifest(arg);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = extract_bfres_switch_manifest(arg);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = extract_cfnt_manifest(arg);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = decode_brfnt_if_possible(arg);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = decode_bflyt_if_possible(arg);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = decode_byml_if_possible(arg);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = decode_image_if_possible(arg);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    err = export_model_if_possible(arg);
    if (err != ERR_NOTHING_TO_DO)
	return err;

    // A native archive's extracted BRSUB members (TEX0/PLT0/CHR0/etc., and
    // MDL0 while the outer two-pass export is deferred) are already the leaf
    // resources we want. Do not cut those detached leaves again when walking
    // an archive's freshly completed output directory.
    {
	// GetByMagicFF deliberately requires at least 0x20 bytes before it
	// classifies BRSUB resources; shorter probes avoid mistaking the small
	// extraction metadata fragments for real TEX0/MDL0/etc. records.
	u8 head[0x40];
	FILE *f = fopen(arg,"rb");
	const size_t got = f ? fread(head,1,sizeof(head),f) : 0;
	if (f) fclose(f);
	const file_format_t leaf_ff = GetByMagicFF(head,(uint)got,0);
	if (  ( got >= 0x20 && IsBRSUB(leaf_ff) )
		|| leaf_ff == FF_PLT0 || leaf_ff == FF_PNG
		|| leaf_ff == FF_BREFT_IMG
		|| ( GetAttribFF(leaf_ff) & FFT_TEXT ) )
	    return ERR_OK;
    }

    // Raw Nintendo codecs are not U8/SZS archives, but users expect the
    // regular extraction front end to handle them as a single extracted file.
    // This also makes a compressed asset found inside an extracted tree usable
    // without switching to a separate command.
    char decompressed_path[PATH_MAX];
    err = decompress_nintendo_file2(arg,decompressed_path,sizeof(decompressed_path));
    if ( err == ERR_OK )
    {
	// Every check above (SARC/GFA/PAC/DARC, BRFNT/BFLYT auto-decode,
	// model export) ran against the still-compressed bytes and correctly
	// declined. decompress_nintendo_file2() wrote the decompressed
	// payload to its own destination path (== 'arg' only when 'arg'
	// already lives under the --dest tree; a genuinely different path
	// otherwise -- recursing on 'arg' itself would just re-read the
	// still-compressed source forever and hit the depth cap). Re-run the
	// same dispatch chain against that destination now that it might
	// actually be one of those -- confirmed necessary on a real retail
	// 3DS disc, where every DARC-bundled layout family ships
	// LZ11-compressed (extract_darc_file() never saw a "darc" magic
	// before this, only the compressed bytes in front of it). The
	// decompressed payload can never itself start with the same codec's
	// magic byte again, so this terminates in one extra hop in practice;
	// the depth cap only guards against a corrupt/adversarial file.
	return depth < 32 ? extract_one_file(decompressed_path,basedir,depth+1) : ERR_FILE_TOO_BIG;
    }
    if (err != ERR_NOTHING_TO_DO)
	return err;

    // Extension-only pass-through claims (.nds/.cia/.3ds/.cci/.cxi/
    // .wad/.app) run only after every native probe declined the file, so
    // a container misnamed as, say, a WAD never masks a real SZS archive.
    if (!opt_no_passthrough)
    {
	char staged_dir[PATH_MAX] = "";
	enumError pas_err = PassthruExtract(arg,pbase,staged_dir,sizeof(staged_dir));
	if (pas_err != ERR_NOTHING_TO_DO)
	    return pas_err == ERR_OK && *staged_dir
		? extract_tree_complete(staged_dir,depth+1)
		: pas_err;
    }

    szs_file_t szs;
    InitializeSZS(&szs);
    err = LoadCreateSZS(&szs,arg,true,opt_ignore>0,false);
    if (!err)
    {
	DASSERT( !szs.file_size || szs.file_size >= szs.size );

	if ( szs.size >= 4 && !memcmp(szs.data,"SARC",4) )
	{
	    err = extract_sarc_mem(arg,basedir,depth,szs.data,szs.size);
	    ResetSZS(&szs);
	    return err;
	}
	if ( szs.size >= 4 && !memcmp(szs.data,"CTPK",4) )
	{
	    err = extract_ctpk_mem(arg,basedir,depth,szs.data,szs.size);
	    ResetSZS(&szs);
	    return err;
	}
	if ( szs.size >= 4 && (!memcmp(szs.data,"NARC",4) || !memcmp(szs.data,"CRAN",4)) )
	{
	    err = extract_narc_mem(arg,basedir,depth,szs.data,szs.size);
	    ResetSZS(&szs);
	    return err;
	}

	if ( analyze_fname && IsBRSUB(szs.fform_arch) )
	    AnalyzeBRSUB(&szs,szs.data,szs.size,arg);

	have_patch_count -= 1000000;
	PRINT("EXTRACT/%s[%s]: %s\n",__FUNCTION__,GetNameFF_SZS(&szs),szs.fname);
	err = ExtractFilesSZS(&szs,0,false,0,basedir);
	have_patch_count += 1000000;
	if (err <= ERR_WARNING)
	{
	    // ExtractFilesSZS writes nested archives as ordinary files. Peel that
	    // completed output immediately; relying on readdir() to notice a new
	    // sibling directory is filesystem/order dependent and missed 1,700
	    // ACCF MDL0s stored inside the Str/Brres archives.
	    char dest[PATH_MAX];
	    get_extract_dest(dest,sizeof(dest),&szs);
	    ccp saved_dest = opt_dest;
	    const int saved_export_count = export_count;
	    opt_dest = 0;
	    export_count = 0;
	    enumError sub_err = extract_tree(dest,depth+1);
	    export_count = saved_export_count;
	    opt_dest = saved_dest;
	    if (err < sub_err) err = sub_err;
	}
	if (err <= ERR_WARNING && export_count > 0)
	{
	    char dest[PATH_MAX];
	    get_extract_dest(dest,sizeof(dest),&szs);
	    ccp saved_dest = opt_dest;
	    opt_dest = 0; // each exported model belongs beside its own source
	    // Even a single BRRES can declare run-time/EFB texture resources that
	    // have no TEX0 payload. Enable the completed archive directory as the
	    // lookup root so COLLADA only emits images that were actually decoded.
	    SetDAETextureSearchRoot(dest);
	    enumError model_err = export_models_tree(dest,0);
	    SetDAETextureSearchRoot(0);
	    opt_dest = saved_dest;
	    if (err < model_err) err = model_err;
	}
	if (err <= ERR_WARNING && OptionUsed[OPT_AUTO])
	{
	    char dest[PATH_MAX];
	    get_extract_dest(dest,sizeof(dest),&szs);
	    ccp saved_dest = opt_dest;
	    opt_dest = 0; // each decoded file belongs beside its own source
	    enumError auto_err = auto_decompress_tree(dest,0);
	    opt_dest = saved_dest;
	    if (err < auto_err) err = auto_err;
	}
    }
    ResetSZS(&szs);
    return depth && err ? ERR_OK : err;
}

// True when PATH is a sibling extraction output: a directory named "<stem>.d"
// whose "<stem>" is a regular file next to it.  The native extractors write
// every decoded archive into a "<source>.d" folder beside its source, and
// those artifacts must not be re-fed into the XX pipeline (a 0-byte
// ".BMG.header" inside a ".bmg.d" was being resubmitted as a BMG, tripping
// the BMG text writer).  Pass-through stage dirs (wit's "<game>.d") have no
// sibling regular file, so this never skips the tree we are told to walk.
static bool is_extractor_output_dir ( ccp path )
{
    const uint n = strlen(path);
    if ( n < 3 || path[n-1] != 'd' || path[n-2] != '.' )
	return false;

    char stem[PATH_MAX];
    snprintf(stem,sizeof(stem),"%.*s",(int)(n-2),path);
    struct stat st;
    if (lstat(stem,&st) == 0 && S_ISREG(st.st_mode))
	return true;

    // U8/RARC/PACK destinations use the source stem (foo.arc -> foo.d), not
    // the complete source filename (foo.arc.d). Recognize any regular sibling
    // named "<stem>.<extension>" so a live parent readdir never feeds the
    // output directory a second time after extract_one_file() already peeled
    // it explicitly above.
    char parent[PATH_MAX], base[PATH_MAX];
    ccp slash = strrchr(stem,'/');
    if (slash)
    {
	snprintf(parent,sizeof(parent),"%.*s",(int)(slash-stem),stem);
	snprintf(base,sizeof(base),"%s",slash+1);
    }
    else
    {
	snprintf(parent,sizeof(parent),".");
	snprintf(base,sizeof(base),"%s",stem);
    }
    DIR *dir = opendir(parent);
    if (!dir) return false;
    const size_t base_len = strlen(base);
    bool found = false;
    struct dirent *de;
    while (!found && (de = readdir(dir)))
    {
	if (strncmp(de->d_name,base,base_len) || de->d_name[base_len] != '.'
	    || !strcmp(de->d_name+base_len,".d"))
	    continue;
	char candidate[PATH_MAX];
	snprintf(candidate,sizeof(candidate),"%s/%s",parent,de->d_name);
	found = !lstat(candidate,&st) && S_ISREG(st.st_mode);
    }
    closedir(dir);
    return found;
}

// Recurse into a directory produced by an external unpacker, peeling every
// regular file found below it exactly like a top-level XX argument.  Each
// decoded file lands beside its own source in the staged tree so the project
// tree stays deterministic for rebuilds.  Sub-directories created by the
// native extractors while unpacking ("<source>.d") are skipped so the walk
// only ever sees the tree the external tool produced.
static enumError extract_tree ( ccp root, uint depth )
{
    if (depth > 32) return ERR_FILE_TOO_BIG;
    DIR *dir = opendir(root);
    if (!dir) return ERR_NOT_EXISTS;
    enumError max_err = ERR_OK;
    struct dirent *de;
    while ((de = readdir(dir)))
    {
	if (de->d_name[0] == '.') continue;
	char path[PATH_MAX];
	const int len = snprintf(path,sizeof(path),"%s/%s",root,de->d_name);
	if (len < 0 || (uint)len >= sizeof(path)) { max_err = ERR_FILE_TOO_BIG; continue; }
	struct stat st;
	if (lstat(path,&st)) { if (max_err < ERR_NOT_EXISTS) max_err = ERR_NOT_EXISTS; continue; }
	enumError err = ERR_OK;
	if (S_ISDIR(st.st_mode))
	{
	    if (!is_extractor_output_dir(path))
		err = extract_tree(path,depth+1);
	}
	else if (S_ISREG(st.st_mode))
	{
	    ccp saved_dest = opt_dest;
	    opt_dest = 0; // each decoded file belongs beside its own source
	    err = extract_one_file(path,0,depth+1);
	    opt_dest = saved_dest;
	}
	if (err != ERR_NOTHING_TO_DO && max_err < err)
	    max_err = err;
    }
    closedir(dir);
    return max_err;
}

static enumError cmd_extract ( enumCommands mode )
{
    ccp basedir = GetOptBasedir();
    if ( mode == CMD_XDECODE )
    {
	RegisterOptionByIndex(&InfoUI_wszst,OPT_DECODE,1,false);
	opt_decode = true;
    }
    else if ( mode == CMD_XEXPORT )
    {
	RegisterOptionByIndex(&InfoUI_wszst,OPT_DECODE,1,false);
	opt_decode = true;
	RegisterOptionByIndex(&InfoUI_wszst,OPT_EXPORT,1,false);
	export_count++;
    }
    else if ( mode == CMD_XALL )
    {
	RegisterOptionByIndex(&InfoUI_wszst,OPT_ALL,1,false);
	opt_recurse	= INT_MAX;
	opt_decode	= true;
	opt_mipmaps	= 1;
    }
    else if ( mode == CMD_XCOMMON )
    {
	opt_recurse	= INT_MAX;
	opt_decode	= false;
	basedir		= "common/";
    }

    if ( opt_dest && !*opt_dest )
	opt_dest = 0;
    opt_mkdir = true;

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);
    enumError max_err = ERR_OK;

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	// XX historically recursed only into directories produced by an external
	// pass-through extractor.  Treat a directory supplied on the command line
	// the same way, so `wszst XX path/to/tree` peels every regular file below
	// it while still skipping sibling "<archive>.d" output directories.
	enumError err;
	if ( IsDirectory(arg,false) && export_count > 0 )
	{
	    err = extract_tree_complete(arg,0);
	}
	else
	    err = IsDirectory(arg,false) ? extract_tree(arg,0)
					 : extract_one_file(arg,basedir,0);
	if ( max_err < err )
	    max_err = err;
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			convert helper			///////////////
///////////////////////////////////////////////////////////////////////////////

static bool ConvertHelper
(
    const u8		* data,		// valid data
    uint		data_size,	// size of 'data'
    ContainerData_t	* cdata,	// NULL or container data
    ccp			src_fname,	// filename of source
    ccp			dest_fname,	// filename of destination
    bool		binary_dest,	// output: false:text, true:binary
    enumError		* err		// not NULL: store error code here
)
{
    enumError temp_err;
    if (!err)
	err = &temp_err;
    *err = ERR_OK;

// [[analyse-magic]]
    const file_format_t fform = GetByMagicFF(data,data_size,data_size);
    switch(fform)
    {
     case FF_BMG:
     case FF_BMG_TXT:
	FreeContainerData(cdata);
	{
	    bmg_t bmg;
	    *err = ScanBMG(&bmg,true,src_fname,data,data_size);
	    if (*err)
		return true;
	    *err = testmode
		? ERR_OK
		: binary_dest
		? SaveRawXBMG(&bmg,dest_fname,true)
		: SaveTextXBMG(&bmg,dest_fname,true);
	    ResetBMG(&bmg);
	}
	return true;

     case FF_GH_ITEM:
     case FF_GH_IOBJ:
     case FF_GH_KART:
     case FF_GH_KOBJ:
     case FF_GH_ITEM_TXT:
     case FF_GH_IOBJ_TXT:
     case FF_GH_KART_TXT:
     case FF_GH_KOBJ_TXT:
	FreeContainerData(cdata);
	{
	    geohit_t geohit;
	    InitializeGEOHIT(&geohit,fform);
	    geohit.fname = src_fname;
	    *err = ScanGEOHIT(&geohit,false,data,data_size);
	    if (*err)
		return false;
	    *err = testmode
		? ERR_OK
		: binary_dest
		? SaveRawGEOHIT(&geohit,dest_fname,true)
		: SaveTextGEOHIT(&geohit,dest_fname,true,minimize_level);
	    geohit.fname = 0;
	    ResetGEOHIT(&geohit);
	}
	return true;

     case FF_KCL:
     case FF_KCL_TXT:
     case FF_WAV_OBJ:
     case FF_SKP_OBJ:
	FreeContainerData(cdata);
	{
	    kcl_t kcl;
	    InitializeKCL(&kcl);
	    kcl.fname = src_fname;
	    *err = ScanKCL(&kcl,false,data,data_size,true,global_check_mode);
	    if (*err)
		return false;
	    *err = testmode
		? ERR_OK
		: binary_dest
		? SaveRawKCL(&kcl,dest_fname,true)
		: SaveTextKCL(&kcl,dest_fname,true);
	    kcl.fname = 0;
	    ResetKCL(&kcl);
	}
	return true;

     case FF_ITEMSLT:
     case FF_ITEMSLT_TXT:
	FreeContainerData(cdata);
	{
	    itemslot_t itemslot;
	    InitializeITEMSLOT(&itemslot,fform);
	    itemslot.fname = src_fname;
	    *err = ScanITEMSLOT(&itemslot,false,data,data_size,src_fname);
	    if (*err)
		return false;
	    *err = testmode
		? ERR_OK
		: binary_dest
		? SaveRawITEMSLOT(&itemslot,dest_fname,true)
		: SaveTextITEMSLOT(&itemslot,dest_fname,true,minimize_level);
	    itemslot.fname = 0;
	    ResetITEMSLOT(&itemslot);
	}
	return true;

     case FF_KMG:
     case FF_KMG_TXT:
	FreeContainerData(cdata);
	{
	    minigame_t minigame;
	    InitializeMINIGAME(&minigame,fform);
	    minigame.fname = src_fname;
	    *err = ScanMINIGAME(&minigame,false,data,data_size);
	    if (*err)
		return false;
	    *err = testmode
		? ERR_OK
		: binary_dest
		? SaveRawMINIGAME(&minigame,dest_fname,true)
		: SaveTextMINIGAME(&minigame,dest_fname,true,minimize_level);
	    minigame.fname = 0;
	    ResetMINIGAME(&minigame);
	}
	return true;

     case FF_KMP:
     case FF_KMP_TXT:
	FreeContainerData(cdata);
	{
	    kmp_t kmp;
	    InitializeKMP(&kmp);
	    kmp.fname = src_fname;
	    *err = ScanKMP(&kmp,false,data,data_size,global_check_mode);
	    if (*err)
		return false;
	    *err = testmode
		? ERR_OK
		: binary_dest
		? SaveRawKMP(&kmp,dest_fname,true)
		: SaveTextKMP(&kmp,dest_fname,true);
	    kmp.fname = 0;
	    ResetKMP(&kmp);
	}
	return true;

     case FF_LEX:
     case FF_LEX_TXT:
	FreeContainerData(cdata);
	{
	    lex_t lex;
	    InitializeLEX(&lex);
	    lex.fname = src_fname;
	    *err = ScanLEX(&lex,false,data,data_size);
	    if (*err)
		return false;
	    *err = testmode
		? ERR_OK
		: binary_dest
		? SaveRawLEX(&lex,dest_fname,true)
		: SaveTextLEX(&lex,dest_fname,true);
	    lex.fname = 0;
	    ResetLEX(&lex);
	}
	return true;

     case FF_MDL:
     case FF_MDL_TXT:	// [[2do]] add format to UI commands BINARY and TEXT
	{
	    mdl_t mdl;
	    InitializeMDL(&mdl);
	    mdl.fname = src_fname;
	 #if USE_NEW_CONTAINER_MDL
	    *err = ScanMDL(&mdl,false,data,data_size,cdata,global_check_mode);
	 #else
	    *err = ScanMDL(&mdl,false,data,data_size,0,global_check_mode);
	 #endif
	    if (*err)
		return false;
	    *err = testmode
		? ERR_OK
		: binary_dest
		? SaveRawMDL(&mdl,dest_fname,true)
		: SaveTextMDL(&mdl,dest_fname,true);
	    mdl.fname = 0;
	    ResetMDL(&mdl);
	}
	return true;

     case FF_OBJFLOW:
     case FF_OBJFLOW_TXT:
	FreeContainerData(cdata);
	{
	    objflow_t objflow;
	    InitializeOBJFLOW(&objflow);
	    objflow.fname = src_fname;
	    *err = ScanOBJFLOW(&objflow,false,data,data_size);
	    if (*err)
		return false;
	    *err = testmode
		? ERR_OK
		: binary_dest
		? SaveRawOBJFLOW(&objflow,dest_fname,true)
		: SaveTextOBJFLOW(&objflow,dest_fname,true,minimize_level);
	    objflow.fname = 0;
	    ResetOBJFLOW(&objflow);
	}
	return true;

     case FF_PAT:
     case FF_PAT_TXT:
	{
	    pat_t pat;
	    InitializePAT(&pat);
	    pat.fname = src_fname;
	 #if USE_NEW_CONTAINER_PAT
	    *err = ScanPAT(&pat,false,data,data_size,cdata,global_check_mode);
	 #else
	    *err = ScanPAT(&pat,false,data,data_size,0,global_check_mode);
	 #endif
	    if (*err)
		return false;
	    *err = testmode
		? ERR_OK
		: binary_dest
		? SaveRawPAT(&pat,dest_fname,true)
		: SaveTextPAT(&pat,dest_fname,true);
	    pat.fname = 0;
	    ResetPAT(&pat);
	}
	return true;

     case FF_BFLYT:
     case FF_BCLYT:
     case FF_BRLYT:
     case FF_BRLAN:
     case FF_BFLYT_TXT:
     case FF_BCLYT_TXT:
	FreeContainerData(cdata);
	{
	    bflyt_t bflyt;
	    InitializeBFLYT(&bflyt);
	    *err = ScanBFLYT(&bflyt,false,data,data_size);
	    if (!*err)
		*err = testmode
		    ? ERR_OK
		    : binary_dest
		    ? SaveRawBFLYT(&bflyt,dest_fname,true)
		    : SaveTextBFLYT(&bflyt,dest_fname,true);
	    ResetBFLYT(&bflyt);
	}
	return true;

     default:
	break;
    }

    *err = ERR_WRONG_FILE_TYPE;
    return false;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command binary/text		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_convert ( bool binary ) // cmd_binary() cmd_text()
{
    static ccp def_path = "\1P/\1N\1?T";
    CheckOptDest(def_path,false);
    char dest[PATH_MAX];
    enumError max_err = ERR_OK;

    raw_data_t raw;
    InitializeRawData(&raw);

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	enumError err = LoadRawData(&raw,false,arg,0,opt_ignore>0,0);
	if ( err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore )
	    continue;
	if ( err > ERR_WARNING )
	{
	    ResetRawData(&raw);
	    return err;
	}

	file_format_t ff_dest = binary
		? GetBinFF(raw.fform) : GetTextFF(raw.fform);

	// Handle layout formats (BFLYT / BCLYT / BRLYT / BRLAN)
	if (!ff_dest && (raw.fform == FF_BFLYT || raw.fform == FF_BCLYT ||
			 raw.fform == FF_BRLYT || raw.fform == FF_BRLAN))
	{
	    if (binary)
		SubstDest( dest, sizeof(dest), arg, opt_dest,
			    "\\1P/\\1N.bflyt", ".bflyt", false );
	    else
		SubstDest( dest, sizeof(dest), arg, opt_dest,
			    "\\1P/\\1N.tflyt", ".tflyt", false );
	    if ( verbose >= 0 )
	    {
		fprintf(stdlog,"%sCREATE/TEXT %s:%s => %s\n",
			verbose > 0 ? "\n" : "",
			GetNameFF(raw.fform,0), arg, dest );
		fflush(stdlog);
	    }
	    if (!testmode)
	    {
		bflyt_t bflyt;
		InitializeBFLYT(&bflyt);
		err = ScanBFLYT(&bflyt,false,raw.data,raw.data_size);
		if (!err)
		    err = binary
			? SaveRawBFLYT(&bflyt,dest,true)
			: SaveTextBFLYT(&bflyt,dest,true);
		ResetBFLYT(&bflyt);
	    }
	    if (err && max_err < err)
		max_err = err;
	    continue;
	}

	if (!ff_dest && raw.data_size >= 16 && (!memcmp(raw.data,"BY",2) || !memcmp(raw.data,"YB",2)))
	{
	    SubstDest( dest, sizeof(dest), arg, opt_dest,
			"\\1P/\\1N.yaml", ".yaml", false );
	    if ( verbose >= 0 )
	    {
		fprintf(stdlog,"%sCREATE/TEXT BYML:%s => %s\n",
			verbose > 0 ? "\n" : "", arg, dest );
		fflush(stdlog);
	    }
	    if (!testmode)
	    {
		File_t F;
		err = CreateFileOpt(&F,true,dest,false,arg);
		if (!F.f) err = ERR_WRITE_FAILED;
		else
		{
		    err = DecodeBYML_YAML(F.f,raw.data,raw.data_size);
		    ResetFile(&F,opt_preserve);
		}
	    }
	    if (err && max_err < err)
		max_err = err;
	    continue;
	}

	if (!ff_dest)
	{
	    ERROR0(ERR_WARNING,"File format %s not supported: %s\n",
			GetNameFF(raw.fform,0), arg );
	    if ( max_err < ERR_WARNING )
		 max_err = ERR_WARNING;
	    continue;
	}

	SubstDest( dest, sizeof(dest), arg, opt_dest,
			def_path, GetExtFF(0,ff_dest), false );

	if ( verbose >= 0 )
	{
	    fprintf(stdlog,"%sCREATE/%s %s:%s => %s:%s\n",
			verbose > 0 ? "\n" : "",
			binary ? "BINARY" : "TEXT",
			GetNameFF(raw.fform,0), arg,
			GetNameFF(ff_dest,0), dest );
	    fflush(stdlog);
	}

	SetupContainerRawData(&raw);
	ConvertHelper(	raw.data, raw.data_size,
			LinkContainerData(&raw.container),
			arg, dest, binary, &err );
	if (err)
	{
	    ERROR0(err,0);
	    if ( max_err < err )
		 max_err = err;
	}
    }

    ResetStringField(&plist);
    ResetRawData(&raw);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command cat			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_cat()
{
    stdlog = stderr;
    enumError max_err = ERR_OK;

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT_SUB);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];
	szs_extract_t eszs;
	enumError err = ExtractSZS(&eszs,true,arg,0,opt_ignore>0);
	PRINT("stat=%u, data=%p, found=%d,%d, path=%s\n",
		err, eszs.data, !eszs.subfile_found, eszs.szs_found, eszs.subpath );

	if (eszs.data)
	{
	    if ( verbose > 0 || testmode )
	    {
		fprintf(stdlog,"%s%sCAT %s:%s\n",
			verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "",
			GetNameFF(eszs.fform_file,eszs.fform_arch),
			eszs.fname );
		fflush(stdlog);
	    }

	    if (!testmode)
	    {
		if (opt_decode)
		{
		    ConvertHelper( eszs.data, eszs.data_size,
					LinkContainerESZS(&eszs),
					eszs.fname, "-", false, &err );
		}
		else
		{
		    size_t wstat = fwrite(eszs.data,1,eszs.data_size,stdout);
		    if ( wstat != eszs.data_size )
			err = ERROR1(ERR_WRITE_FAILED,"Write to stdout failed\n");
		}
	    }
	}
	else if ( !err && !opt_ignore )
	    err = PrintErrorExtractSZS(&eszs,arg);
	fflush(stdout);

	if ( max_err < err )
	     max_err = err;
	ResetExtractSZS(&eszs);
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command <FFORM>			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_FFORM ( file_format_t fform, ccp autoname )
{
    stdlog = stderr;
    //char pathbuf[PATH_MAX];
    enable_kcl_drop_auto++;

    enumError max_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT_SUB);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	szs_extract_t eszs;
	enumError err = ExtractSZS(&eszs,true,arg,autoname,opt_ignore>0);

// [[analyse-magic]]
	if ( eszs.data && GetByMagicFF(eszs.data,eszs.data_size,eszs.data_size) == fform )
	{
	    if ( verbose > 0 || testmode )
		fprintf(stdlog,"%s%sCAT %s:%s\n",
			verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "",
			GetNameFF(eszs.fform_file,eszs.fform_arch),
			eszs.fname );

	    if (!testmode)
		ConvertHelper( eszs.data, eszs.data_size,
				LinkContainerESZS(&eszs),
				eszs.fname, "-", false, &err );
	}
	else if ( !err && !opt_ignore )
	    err = PrintErrorExtractSZS(&eszs,arg);

	if ( max_err < err )
	     max_err = err;
	ResetExtractSZS(&eszs);
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command info			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_info()
{
    stdlog = stderr;

    char pathbuf[PATH_MAX];
    ccp search_tab[] = { "info.txt", "credits.txt", 0 };

    szs_extract_t eszs;
    InitializeExtractSZS(&eszs);

    enumError max_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	ccp * search;
	for ( search = search_tab; ; search++ )
	{
	    if (!*search)
	    {
		if (!opt_ignore)
		    printf("\n* NO INFO FOUND: %s\n\n",arg);
		max_err = ERR_WARNING;
		break;
	    }

	    ccp path = PathCatPP(pathbuf,sizeof(pathbuf),arg,*search);
	    ResetExtractSZS(&eszs);
	    enumError err = ExtractSZS(&eszs,false,path,0,true);
	    if ( !err && eszs.data )
	    {
		if (print_header)
		    printf("\n* %s\n\n",eszs.fname);
		fwrite(eszs.data,1,eszs.data_size,stdout);
		if ( eszs.data_size && eszs.data[eszs.data_size-1] != '\n' )
		    putchar('\n');
		if (print_header)
		    putchar('\n');
		break;
	    }
	}
    }

    ResetStringField(&plist);
    ResetExtractSZS(&eszs);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command ghost			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_ghost()
{
    static ccp def_path = "\1P/\1F\1?T";
    CheckOptDest(def_path,false);
    char dest[PATH_MAX];

    raw_data_t raw;
    InitializeRawData(&raw);

    rkg_info_t ri;
    InitializeRKGInfo(&ri);
    enumError max_err = ERR_OK;

    File_t fo;
    InitializeFile(&fo);

    PrintScript_t ps;
    SetupPrintScriptByOptions(&ps);

    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];
	enumError err = LoadRawData(&raw,false,arg,0,opt_ignore>0,0);
	if ( err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore )
	    continue;
	if ( err > ERR_WARNING )
	    return err;

	if (!fo.f)
	    SubstDest(dest,sizeof(dest),arg,opt_dest,def_path,
			GetExtFF(script_fform,0),false);

	if ( verbose >= 0 )
	{
	    fprintf(stdlog,"%sANALYZE %s:%s => %s:%s\n",
			verbose > 0 ? "\n" : "",
			GetNameFF(raw.fform,0), raw.fname,
			GetNameFF(script_fform,0), dest );
	    fflush(stdlog);
	}

	err = ScanRawDataGHOST(&ri,false,&raw);
	if ( err >= ERR_WARNING )
	{
	    if ( max_err < err )
		 max_err = err;
	    continue;
	}

	if (!fo.f)
	{
	    enumError err = CreateFile(&fo,false,dest,FM_STDIO|FM_OVERWRITE);
	    if (err)
	    {
		max_err = err;
		break;
	    }
	    ps.f = fo.f;
	    PrintScriptHeader(&ps);
	}

	PrintScriptVars(&ps,3,
		"compressed=%d\n"
		"size=%u\n"
		"score=%u\n"
		"time_button=%u\n"
		"time_direction=%u\n"
		"time_trick=%u\n"
		,ri.is_compressed
		,ri.data_size
		,ri.score
		,ri.time_but
		,ri.time_dir
		,ri.time_trick
		);
 #if 0
	printf("score = %u, compr = %d, size = %u\n",
		ri.score, ri.is_compressed, ri.data_size );
	printf("but = %u,%u, dir = %u,%u, trick=%u,%u\n",
		ri.n_but, ri.time_but,
		ri.n_dir, ri.time_dir,
		ri.n_trick, ri.time_trick );
 #endif

	if (!script_array)
	{
	    PrintScriptFooter(&ps);
	    ps.f = 0;
	    ResetFile(&fo,0);
	}
    }

    PrintScriptFooter(&ps);

    ResetStringField(&plist);
    ResetPrintScript(&ps);
    ResetFile(&fo,0);
    ResetRKGInfo(&ri);
    ResetRawData(&raw);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command decompress		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_yazdump()
{
    CheckOptDest("-",false);

    enumError max_err = ERR_OK;
    StringField_t plist = {0};
    CollectExpandParam(&plist,first_param,-1,WM__DEFAULT);

    for ( int argi = 0; argi < plist.used; argi++ )
    {
	ccp arg = plist.field[argi];

	szs_file_t szs;
	InitializeSZS(&szs);
	enumError err = LoadSZS(&szs,arg,false,opt_ignore>0,false);

	if ( err <= ERR_WARNING && err != ERR_NOT_EXISTS
		&& ( szs.fform_file == FF_YAZ0 || szs.fform_file == FF_YAZ1 ))
	{
	    char dest[PATH_MAX];
	    SubstDest(dest,sizeof(dest),arg,opt_dest,".txt",".txt",false);

	    File_t F;
	    CreateFileOpt(&F,true,dest,testmode,0);
	    if (F.f)
	    {
		if ( verbose >= 0 || testmode )
		    fprintf(F.f,"%sYAZ DUMP of %s:%s\n",
				verbose > 0 ? "\n" : "",
				GetNameFF_SZS(&szs), arg );
		err = DecompressSZS(&szs,true,F.f);
	    }
	}

	if ( max_err < err )
	     max_err = err;
	ResetSZS(&szs);
    }

    ResetStringField(&plist);
    return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command vehicle			///////////////
///////////////////////////////////////////////////////////////////////////////

#ifndef HAVE_WIIMM_EXT

 enumError cmd_vehicle()
 {
    ERROR0(ERR_NOT_IMPLEMENTED,
	"Command VEHICLE not implemented in this version!\n");
    ExitFixed(ERR_NOT_IMPLEMENTED);
 }

#endif

//
///////////////////////////////////////////////////////////////////////////////
///////////////                   check options                 ///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError CheckOptions ( int argc, char ** argv, bool is_env )
{
    TRACE("CheckOptions(%d,%p,%d) optind=%d\n",argc,argv,is_env,optind);

    optind = 0;
    int err = 0;
    ccp filter_bmg = 0;

    for(;;)
    {
      const int opt_stat = getopt_long(argc,argv,OptionShort,OptionLong,0);
      if ( opt_stat == -1 )
	break;

      RegisterOptionByName(&InfoUI_wszst,opt_stat,1,is_env);

      switch ((enumGetOpt)opt_stat)
      {
	case GO__ERR:		err++; break;

	case GO_VERSION:	version_exit();
	case GO_HELP:		help_exit(false);
	case GO_XHELP:		help_exit(true);
	case GO_CONFIG:		opt_config = optarg; break;
	case GO_YDEBUG:		enable_ydebug++; break;
	case GO_ALLOW_ALL:	allow_all = true; break;
	case GO_COMPATIBLE:	err += ScanOptCompatible(optarg); break;
	case GO_WIDTH:		err += ScanOptWidth(optarg); break;
	case GO_MAX_WIDTH:	err += ScanOptMaxWidth(optarg); break;
	case GO_NO_PAGER:	opt_no_pager = true; break;
	case GO_ZERO:		opt_zero++; break;
	case GO_QUIET:		verbose = verbose > -1 ? -1 : verbose - 1; break;
	case GO_VERBOSE:	verbose = verbose <  0 ?  0 : verbose + 1; break;
	case GO_LOGGING:	logging++; break;
	case GO_EXT_ERRORS:	ext_errors++; break;
	case GO_TIMING:		log_timing++; break;
	case GO_WARN:		err += ScanOptWarn(optarg); break;
	case GO_DE:		use_de = true; break;
	case GO_CT_CODE:	ctcode_enabled = true; break;
	case GO_LE_CODE:	lecode_enabled = true; break; // optional argument ignored
	case GO_LE_04X:		lecode_04x = true; break;
	case GO_COLORS:		err += ScanOptColorize(0,optarg,0); break;
	case GO_NO_COLORS:	opt_colorize = COLMD_OFF; break;

	case GO_CHDIR:		err += ScanOptChdir(optarg); break;
	case GO_CONST:		err += ScanOptConst(optarg); break;
	case GO_TOUCH:		break;
	case GO_AUTO:		break;
	case GO_LOAD_PREFIX:	err += ScanOptLoadPrefix(optarg); break;
	case GO_LOAD_CATEGORY:	err += ScanOptLoadCategory(optarg); break;
	case GO_PLUS:		opt_plus = optarg; break;
	case GO_SPLIT:		opt_split = str2l(optarg,0,10); break;
	case GO_PRINTF:		opt_printf = optarg; break;
	case GO_SET_FLAGS:	err += ScanOptSetFlags(optarg); break;
	case GO_SET_SCALE:	err += ScanOptSetScale(optarg); break;
	case GO_SET_ROT:	err += ScanOptSetRot(optarg); break;
	case GO_SET_X:		err += ScanOptSet(0,optarg); break;
	case GO_SET_Y:		err += ScanOptSet(1,optarg); break;
	case GO_SET_Z:		err += ScanOptSet(2,optarg); break;
	case GO_XCENTER:	break;
	case GO_YCENTER:	break;
	case GO_ZCENTER:	break;
	case GO_CENTER:		break;
	case GO_SCALE:		err += ScanOptScale(optarg); break;
	case GO_SHIFT:		err += ScanOptShift(optarg); break;
	case GO_XSS:		err += ScanOptXSS(0,optarg); break;
	case GO_YSS:		err += ScanOptXSS(1,optarg); break;
	case GO_ZSS:		err += ScanOptXSS(2,optarg); break;
	case GO_ROT:		err += ScanOptRotate(optarg); break;
	case GO_XROT:		err += ScanOptXRotate(0,optarg); break;
	case GO_YROT:		err += ScanOptXRotate(1,optarg); break;
	case GO_ZROT:		err += ScanOptXRotate(2,optarg); break;
	case GO_YPOS:		err += ScanOptYPos(optarg); break;
	case GO_TRANSLATE:	err += ScanOptTranslate(optarg); break;
	case GO_NULL:		force_transform |= 1; break;
	case GO_NEXT:		err += NextTransformation(false); break;
	case GO_ASCALE:		err += ScanOptAScale(optarg); break;
	case GO_AROT:		err += ScanOptARotate(optarg); break;
	case GO_TFORM_SCRIPT:	err += ScanOptTformScript(optarg); break;
	case GO_RM_GOBJ:	err += ScanOptRmGobj(optarg); break;
	case GO_BATTLE:		err += ScanOptBattle(optarg); break;
	case GO_EXPORT_FLAGS:	err += ScanOptExportFlags(optarg); break;
	case GO_ROUTE_OPTIONS:	err += ScanOptRouteOptions(optarg); break;
	case GO_WIM0:		err += ScanOptWim0(optarg); break;
	case GO_SLOT:		err += ScanOptSlot(optarg); break;
	case GO_LOAD_KCL:	err += ScanOptLoadKcl(optarg); break;
	case GO_KCL:		err += ScanOptKcl(optarg); break;
	case GO_KCL_FLAG:	err += ScanOptKclFlag(optarg); break;
	case GO_KCL_SCRIPT:	err += ScanOptKclScript(optarg); break;
	case GO_TRI_AREA:	err += ScanOptTriArea(optarg); break;
	case GO_TRI_HEIGHT:	err += ScanOptTriHeight(optarg); break;
	case GO_FLAG_FILE:	opt_flag_file = optarg; break;
	case GO_XTRIDATA:	opt_xtridata = optarg ? str2ul(optarg,0,10) : 1; break;
	case GO_KMP:		err += ScanOptKmp(optarg); break;
	case GO_N_LAPS:		err += ScanOptNLaps(optarg); break;
	case GO_SPEED_MOD:	err += ScanOptSpeedMod(optarg); break;
	case GO_KTPT2:		err += ScanOptKtpt2(optarg); break;
	case GO_TFORM_KMP:	err += ScanOptTformKmp(optarg); break;
	case GO_GAMEMODES:	err += ScanOptGamemodes(optarg); break;
	case GO_REPAIR_XPF:	err += ScanOptRepairXPF(optarg); break;

	case GO_MDL:		err += ScanOptMdl(optarg); break;
	case GO_MINIMAP:	break;
	case GO_PAT:		err += ScanOptPat(optarg); break;
	case GO_PATCH_FILES:	err += ScanOptPatchFiles(optarg); break;

	case GO_KMG_LIMIT:	err += ScanOptKmgLimit(optarg); break;
	case GO_KMG_COPY:	err += ScanOptKmgCopy(optarg); break;

	case GO_LT_CLEAR:	opt_lt_clear = true; break;
	case GO_LT_ONLINE:	err += ScanOptLtOnline(optarg); break;
	case GO_LT_N_PLAYERS:	err += ScanOptLtNPlayers(optarg); break;
	case GO_LT_COND_BIT:	err += ScanOptLtCondBit(optarg); break;
	case GO_LT_GAME_MODE:	err += ScanOptLtGameMode(optarg); break;
	case GO_LT_ENGINE:	err += ScanOptLtEngine(optarg); break;
	case GO_LT_RANDOM:	err += ScanOptLtRandom(optarg); break;
	case GO_LEX_FEATURES:	opt_lex_features = true; break;
	case GO_LEX_RM_FEAT:	opt_lex_rm_features = true; break;
	case GO_LEX_PURGE:	opt_lex_purge = true; break;

	case GO_UTF_8:		use_utf8 = true; break;
	case GO_NO_UTF_8:	use_utf8 = false; break;

	case GO_TEST:		testmode++; break;
	case GO_FORCE:		force_count++; break;
	case GO_REPAIR_MAGICS:	err += ScanOptRepairMagic(optarg); break;
	case GO_TINY:		err += ScanOptTiny(optarg); break;

	case GO_ANALYZE:	analyze_fname = optarg; break;
	case GO_ANALYZE_MODE:	err += ScanOptAnalyzeMode(optarg); break;

 #if OPT_OLD_NEW
	case GO_OLD:		opt_new = opt_new>0 ? -1 : opt_new-1; break;
	case GO_STD:		opt_new = 0; break;
	case GO_NEW:		opt_new = opt_new<0 ? +1 : opt_new+1; break;
 #endif
	case GO_EXTRACT:	opt_extract = optarg; break;

	case GO_ESC:		err += ScanEscapeChar(optarg) < 0; break;
	case GO_SOURCE:		SetSource(optarg); break;
	case GO_ID_LIST:	SetIdList(optarg); break;
	case GO_REFERENCE:	SetReference(optarg); break;
	case GO_DEST:		SetDest(optarg,false); break;
	case GO_DEST2:		SetDest(optarg,true); break;
	case GO_PARENT:		opt_parent = optarg; break;
	case GO_OVERWRITE:	opt_overwrite = true; break;
	case GO_NUMBER:		opt_number = true; break;
	case GO_REMOVE_SRC:	opt_remove_src = true; break;
	case GO_REMOVE_DEST:	opt_remove_dest = true; break;
	case GO_UPDATE:		opt_update = true; break;
	case GO_PRESERVE:	opt_preserve = true; break;
	case GO_IGNORE:		opt_ignore++; break;
	case GO_IGNORE_SETUP:	opt_ignore_setup = true; break;
	case GO_PURGE:		opt_purge = true; break;

	case GO_YAZ0:		SetCompressionFF(FF_INVALID,FF_YAZ0); break;
	case GO_YAZ1:		SetCompressionFF(FF_INVALID,FF_YAZ1); break;
	case GO_XYZ:		SetCompressionFF(FF_INVALID,FF_XYZ); break;
	case GO_BZ:		SetCompressionFF(FF_INVALID,FF_BZ); break;
	case GO_BZIP2:		SetCompressionFF(FF_INVALID,FF_BZIP2); break;
	case GO_CYBZ:		SetCompressionFF(FF_INVALID,FF_YBZ); break;
	case GO_LZ:		SetCompressionFF(FF_INVALID,FF_LZ); break;
	case GO_LZMA:		SetCompressionFF(FF_INVALID,FF_LZMA); break;
	case GO_CYLZ:		SetCompressionFF(FF_INVALID,FF_YLZ); break;

	case GO_U8:		SetCompressionFF(FF_U8,FF_INVALID); break;
	case GO_SZS:		SetCompressionFF(FF_U8,FF_YAZ0); break;
	case GO_WU8:		SetCompressionFF(FF_WU8,FF_INVALID); break;
	case GO_XWU8:		SetCompressionFF(FF_WU8,FF_XYZ); break;
	case GO_WBZ:		SetCompressionFF(FF_WU8,FF_BZ); break;
	case GO_WLZ:		SetCompressionFF(FF_WU8,FF_LZ); break;
	case GO_YBZ:		SetCompressionFF(FF_U8,FF_YBZ); break;
	case GO_YLZ:		SetCompressionFF(FF_U8,FF_YLZ); break;
	//case GO_LTA:		SetCompressionFF(FF_LTA,FF_INVALID); break;
	case GO_LFL:		SetCompressionFF(FF_LFL,FF_INVALID); break;
	//case GO_ARC:		SetCompressionFF(FF_RARC,FF_INVALID); break;
	case GO_BRRES:		SetCompressionFF(FF_BRRES,FF_INVALID); break;
	case GO_BREFF:		SetCompressionFF(FF_BREFF,FF_INVALID); break;
	case GO_BREFT:		SetCompressionFF(FF_BREFT,FF_INVALID); break;
	case GO_PACK:		SetCompressionFF(FF_PACK,FF_INVALID); break;

	case GO_JSON:		script_fform = FF_JSON; break;
	case GO_SH:		script_fform = FF_SH; break;
	case GO_BASH:		script_fform = FF_BASH; break;
	case GO_PHP:		script_fform = FF_PHP; break;
	case GO_MAKEDOC:	script_fform = FF_MAKEDOC; break;
	case GO_VAR:		script_varname = optarg; break;
	case GO_ARRAY:		script_array++; break;
	case GO_AVAR:		script_array++; script_varname = optarg; break;
	case GO_CASE:		err += ScanOptCase(optarg); break;
	case GO_FMODES:		err += ScanOptFModes(optarg); break;
	case GO_INSTALL:	opt_install++; break;

	case GO_PT_DIR:		err += ScanOptPtDir(optarg); break;
	case GO_LINKS:		opt_links = true; break;
	case GO_RM_AIPARAM:	opt_rm_aiparam = true; break;
	case GO_ALIGN_U8:	err += ScanOptAlignU8(optarg); break;
	case GO_ALIGN_LTA:	err += ScanOptAlignLTA(optarg); break;
	case GO_ALIGN_PACK:	err += ScanOptAlignPACK(optarg); break;
	case GO_ALIGN_BRRES:	err += ScanOptAlignBRRES(optarg); break;
	case GO_ALIGN_BREFF:	err += ScanOptAlignBREFF(optarg); break;
	case GO_ALIGN_BREFT:	err += ScanOptAlignBREFT(optarg); break;
	case GO_ALIGN:		err += ScanOptAlign(optarg); break;
	case GO_TRANSFORM:	err += ScanOptTransform(optarg); break;
	case GO_STRIP:		opt_strip = true; break;

	case GO_NO_COMPRESS:	opt_compr_mode = -1; break;
	case GO_COMPRESS:	err += ScanOptCompr(optarg); break;
	case GO_FAST:		opt_fast = true; err += ScanOptCompr("fast"); break;
	case GO_NORM:		opt_norm = true; break;
	case GO_NO_COPY:	opt_no_copy = true; break;
	case GO_MAX_FILE_SIZE:	err += ScanOptMaxFileSize(optarg); break;
	case GO_TRACKS:		err += ScanOptTracks(optarg); break;
	case GO_ARENAS:		err += ScanOptArenas(optarg); break;
	case GO_LOAD_BMG:	err += ScanOptLoadBMG(optarg); break;
	case GO_PATCH_BMG:	err += ScanOptPatchMessage(optarg); break;
	case GO_MACRO_BMG:	err += ScanOptMacroBMG(optarg); break;
	case GO_FILTER_BMG:	filter_bmg = optarg; break;
	case GO_LE_MENU:	opt_le_menu = true; break;
	case GO_9LAPS:		opt_9laps = true; break;
	case GO_UI_SOURCE:	opt_ui_source = optarg; break;
	case GO_CUP_ICONS:	opt_cup_icons = optarg; break;
	case GO_TITLE_SCREEN:	opt_title_screen = optarg; break;
	case GO_AUTOADD_PATH:	DefineAutoAddPath(optarg); break;

	case GO_NO_PASSTHROUGH:	opt_no_passthrough = true; break;
	case GO_WITH_WIT:	opt_with_wit = optarg; break;
	case GO_WITH_NDSTOOL:	opt_with_ndstool = optarg; break;
	case GO_WITH_CTRTOOL:	opt_with_ctrtool = optarg; break;
	case GO_WITH_SHARPII:	opt_with_sharpii = optarg; break;
	case GO_WITH_HACTOOL:	opt_with_hactool = optarg; break;
	case GO_WITH_BMS:	opt_with_bms = optarg; break;

	case GO_ENCODE_ALL:	opt_encode_all = true; break;
	case GO_ENCODE_IMG:	opt_encode_img = true; break;
	case GO_NO_ENCODE:	opt_no_encode = true; break;
	case GO_NO_RECURSE:	opt_no_recurse = true; break;
	case GO_AUTO_ADD:	opt_auto_add = true; break;
	case GO_NO_ECHO:	opt_no_echo = true; break;
	case GO_NO_CHECK:	opt_no_check = true; break;
	case GO_BASEDIR:	opt_basedir = optarg; break;
	case GO_RECURSE:	err += ScanOptRecurse(optarg); break;
	case GO_EXT:		opt_ext++; break;
	case GO_DECODE:		opt_decode = true; break;
	case GO_CMPR_DEFAULT:	err += ScanOptCmprDefault(optarg); break;
	case GO_N_MIPMAPS:	err += ScanOptNMipmaps(optarg); break;
	case GO_MAX_MIPMAPS:	err += ScanOptMaxMipmaps(optarg); break;
	case GO_MIPMAP_SIZE:	err += ScanOptMipmapSize(optarg); break;
	case GO_MIPMAPS:	opt_mipmaps = +1; break;
	case GO_NO_MIPMAPS:	opt_mipmaps = -1; break;
	case GO_FAST_MIPMAPS:	fast_resize_enabled = true; break;
	case GO_CUT:		opt_cut = true; break;
	case GO_RAW:		opt_raw = true; break;

	case GO_ROUND:		opt_round = true; break;
	case GO_LONG:		long_count++; break;
//	case GO_FULL:		full_count++; break;
	case GO_EXPORT:		export_count++; break;
	case GO_SORT:		err += ScanOptSort(optarg); break;
	case GO_NO_HEADER:	print_header = false; break;
	case GO_BRIEF:		brief_count++; break;
	case GO_NO_WILDCARDS:	no_wildcards_count++; break;
	case GO_IN_ORDER:	inorder_count++; break;
	case GO_PIPE:		pipe_count++; break;
	case GO_DELTA:		delta_count++; break;
	case GO_DIFF:		diff_count++; break;
	case GO_NO_PARAM:	print_param = false; break;
	case GO_EPSILON:	err += ScanOptEpsilon(optarg); break;

	case GO_BMG_ENDIAN:	err += ScanOptBmgEndian(optarg); break;
	case GO_BMG_ENCODING:	err += ScanOptBmgEncoding(optarg); break;
	case GO_BMG_INF_SIZE:	err += ScanOptBmgInfSize(optarg,false); break;
	case GO_BMG_MID:	err += ScanOptBmgMid(optarg); break;
	case GO_FORCE_ATTRIB:	err += ScanOptForceAttrib(optarg); break;
	case GO_DEF_ATTRIB:	err += ScanOptDefAttrib(optarg); break;
	case GO_NO_ATTRIB:	opt_bmg_no_attrib = true; break;
	case GO_X_ESCAPES:	opt_bmg_x_escapes = true; break;
	case GO_OLD_ESCAPES:	opt_bmg_old_escapes = true; break;
	case GO_SINGLE_LINE:	opt_bmg_single_line++; break;
	case GO_NO_BMG_COLORS:	opt_bmg_colors = 0; break;
	case GO_BMG_COLORS:	opt_bmg_colors = 2; break;
	case GO_NO_BMG_INLINE:	opt_bmg_inline_attrib = false; break;
	case GO_CACHE:		opt_cache = optarg; opt_remove_dest = true; break;
	case GO_CNAME:		opt_cname = optarg; break;
	case GO_LOG_CACHE:	opt_log_cache = optarg; break;
	case GO_PARALLEL:	parallel_count++; break;
	case GO_ID:		opt_id = true; break;
	case GO_BASE64:		opt_base64 = true; err += ScanOptCoding64(optarg); break;
	case GO_DB64:		opt_db64 = true; err += ScanOptCoding64(optarg); break;
	case GO_CODING:		err += ScanOptCoding64(optarg); break;
	case GO_VERIFY:		opt_verify = true; break;
	case GO_SECTIONS:	print_sections++; break;
	case GO_ALL:		set_all(0); break;

	// no default case defined
	//	=> compiler checks the existence of all enum values
      }
    }

    if ( opt_compr_mode == -9 )
    {
// [[?]]
	if (opt_compr)
	    opt_colorize = 1;
	list_compressions_exit();
    }

 #ifdef DEBUG
    DumpUsedOptions(&InfoUI_wszst,TRACE_FILE,11);
 #endif
    CloseTransformation();
    NormalizeOptions( verbose > 3 && !is_env );
    SetupBMG(filter_bmg);
    SetupKCL();
    SetupKMP();
    SetupMDL();

    // [[cache]] p2=true until final version of 'post-patch-mkw.sh'
    SetupSZSCache(opt_cache,true);

    if (!err)
    {
	enumError err = SetupPatchingListBMG();
	if ( err > ERR_WARNING )
	    return err;
    }

    return !err ? ERR_OK : ProgInfo.max_error ? ProgInfo.max_error : ERR_SYNTAX;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                   check command                 ///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError CheckCommand ( int argc, char ** argv )
{
    const KeywordTab_t * cmd_ct = CheckCommandHelper(argc,argv,CommandTab);
    if (!cmd_ct)
	hint_exit(ERR_SYNTAX);

    TRACE("COMMAND FOUND: #%lld = %s\n",(u64)cmd_ct->id,cmd_ct->name1);
    current_command = cmd_ct;

    if (!allow_all)
    {
	enumError err = VerifySpecificOptions(&InfoUI_wszst,cmd_ct);
	if (err)
	    hint_exit(err);
    }
    WarnDepractedOptions(&InfoUI_wszst);

    if ( cmd_ct->id != CMD_ARGTEST )
    {
	argc -= optind+1;
	argv += optind+1;

	if ( cmd_ct->id == CMD_TEST )
	    while ( argc-- > 0 )
		AddParam(*argv++);
	else
	    while ( argc-- > 0 )
		AtFileHelper(*argv++,AddParam);
    }

    enumError err = ERR_OK;
    switch ((enumCommands)cmd_ct->id)
    {
	case CMD_VERSION:	version_exit();
	case CMD_HELP:		PrintHelpColor(&InfoUI_wszst); break;
	case CMD_CONFIG:	err = cmd_config(); break;
	case CMD_INSTALL:	err = cmd_install(); break;
	case CMD_ARGTEST:	err = cmd_argtest(argc,argv); break;
	case CMD_EXPAND:	err = cmd_expand(argc,argv); break;
	case CMD_WILDCARDS:	err = cmd_wildcards(argc,argv); break;
	case CMD_TEST:		err = cmd_test(); break;
	case CMD_COLORS:	err = Command_COLORS(brief_count?-brief_count:long_count,0,0); break;
	case CMD_ERROR:		err = cmd_error(); break;
	case CMD_FILETYPE:	err = cmd_filetype(); break;
	case CMD_UI_CHECK:	err = cmd_ui_check(); break;
	case CMD_FILEATTRIB:	err = cmd_fileattrib(); break;
	case CMD_BRSUB:		err = cmd_brsub(); break;

	case CMD_SYMBOLS:	err = DumpSymbols(SetupParserVars()); break;
	case CMD_FUNCTIONS:	SetupReferenceKCL(0);
				err = ListParserFunctions(); break;
	case CMD_CALCULATE:	err = ParserCalc(SetupParserVars()); break;
	case CMD_MATRIX:	err = cmd_matrix(); break;
	case CMD_FLOAT:		err = cmd_float(); break;
	case CMD_VR_CALC:	err = cmd_vr_calc(); break;
	case CMD_VR_RACE:	err = cmd_vr_race(); break;
	case CMD_AUTOADD:	err = cmd_autoadd(); break;
	case CMD_TRACKS:	err = cmd_tracks(); break;
	case CMD_SCANCACHE:	err = cmd_scancache(); break;
	case CMD_EXPORT:	err = cmd_export(); break;
	case CMD_SIZEOF:	err = cmd_sizeof(); break;
	case CMD_CODE:		err = cmd_code(); break;
	case CMD_RECODE:	err = cmd_recode(); break;
	case CMD_SUBFILE:	err = cmd_subfile(); break;
	case CMD_TESTNORM:	err = cmd_testnorm(); break;

	case CMD_LIST:		err = cmd_list(0); break;
	case CMD_LIST_L:	err = cmd_list(1); break;
	case CMD_LIST_LL:	err = cmd_list(2); break;
	case CMD_LIST_LLL:	err = cmd_list(3); break;
	case CMD_LIST_A:	set_all(1); err = cmd_list(1); break;
	case CMD_LIST_LA:	set_all(1); err = cmd_list(2); break;

	case CMD_NAME_REF:	err = cmd_name_ref(); break;

	case CMD_ILIST:		err = cmd_ilist(0); break;
	case CMD_ILIST_L:	err = cmd_ilist(1); break;
	case CMD_ILIST_LL:	err = cmd_ilist(2); break;
	case CMD_ILIST_A:	set_all(0); err = cmd_ilist(0); break;
	case CMD_ILIST_LA:	set_all(0); err = cmd_ilist(1); break;

	case CMD_DUMP:		err = cmd_dump(); break;
	case CMD_MEMORY:	err = cmd_memory(); break;
	case CMD_MEMORY_A:	set_all(1); err = cmd_memory(); break;
	case CMD_SHA1:		err = cmd_sha1(); break;
	case CMD_ANALYZE:	err = cmd_analyze(); break;
	case CMD_SPLIT:		err = cmd_split(); break;
	case CMD_IS_TEXTURE:	err = cmd_is_texture(); break;
	case CMD_FEATURES:	err = cmd_features(); break;
	case CMD_DISTRIBUTION:	err = cmd_distribution(); break;
	case CMD_DIFF:		err = cmd_diff(); break;

	case CMD_CHECK:		err = cmd_check(); break;
	case CMD_SLOTS:		err = cmd_slots(); break;
	case CMD_STGI:		err = cmd_stgi(); break;
	case CMD_IS_ARENA:	err = cmd_is_arena(); break;
	case CMD_PATCH:		err = cmd_patch(); break;
	case CMD_COPY:		err = cmd_copy(); break;
	case CMD_DUPLICATE:	err = cmd_duplicate(); break;
	case CMD_NORMALIZE:	err = cmd_normalize(); break;
	case CMD_MINIMAP:	err = cmd_minimap(); break;
	case CMD_COMPRESS:	err = cmd_compress(); break;
	case CMD_DECOMPRESS:	err = cmd_decompress(); break;
	case CMD_ENCODE:	err = cmd_create(false); break;
	case CMD_CREATE:	err = cmd_create(true); break;
	case CMD_UPDATE:	err = cmd_update(); break;
	case CMD_EXTRACT:
	case CMD_XDECODE:
	case CMD_XEXPORT:
	case CMD_XALL:
	case CMD_XCOMMON:	err = cmd_extract(cmd_ct->id); break;

	case CMD_WC24DECRYPT:	err = cmd_wc24(false); break;
	case CMD_WC24ENCRYPT:	err = cmd_wc24(true); break;
	case CMD_BMS:		err = cmd_bms(); break;
	case CMD_SPRITES:	err = cmd_sprites(); break;
	case CMD_BCH:		err = cmd_bch(); break;

	case CMD_BINARY:	err = cmd_convert(true); break;
	case CMD_TEXT:		err = cmd_convert(false); break;
	case CMD_CAT:		err = cmd_cat(); break;
	case CMD_BMG:		err = cmd_bmg_cat(true); break;
	case CMD_KCL:		err = cmd_FFORM(FF_KCL,"course.kcl"); break;
	case CMD_KMP:		err = cmd_FFORM(FF_KMP,"course.kmp"); break;
	case CMD_LEX:		err = cmd_FFORM(FF_LEX,"course.lex"); break;
	case CMD_INFO:		err = cmd_info(); break;
	case CMD_GHOST:		err = cmd_ghost(); break;
	case CMD_YAZDUMP:	err = cmd_yazdump(); break;

	case CMD_VEHICLE:	err = cmd_vehicle(); break;

	// no default case defined
	//	=> compiler checks the existence of all enum values

	case CMD__NONE:
	case CMD__N:
	    help_exit(false);
    }

    SaveSZSCache(false);
    return PrintErrorStat(err,verbose,cmd_ct->name1);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			main_wszst(), main()		///////////////
///////////////////////////////////////////////////////////////////////////////

int main_wszst ( int argc, char ** argv )
{
    tool_name = "wszst";
    print_title_func = print_title;
    SetupLib(argc,argv,WSZST_SHORT,VERSION,TITLE);
    SetupExtendedSZS();

    //----- process arguments

    if ( argc < 2 )
    {
	printf("\n%s\n%s\nVisit %s%s for more info.\n\n",
		text_logo, TITLE, URI_HOME, WSZST_SHORT );
	hint_exit(ERR_OK);
    }

    enumError err = CheckEnvOptions2("WSZST_OPT",CheckOptions);
    if (err)
	hint_exit(err);

    err = CheckOptions(argc,argv,false);
    if (err)
	hint_exit(err);

    err = CheckCommand(argc,argv);
    CloseAnalyzeFile();
    DUMP_TRACE_ALLOC(TRACE_FILE);

    if (SIGINT_level)
	err = ERROR0(ERR_INTERRUPT,"Program interrupted by user.");
    return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			tool wrapper			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef int (*main_func) ( int argc, char ** argv );

extern int main_wszst ( int argc, char ** argv );
extern int main_wbmgt ( int argc, char ** argv );
extern int main_wctct ( int argc, char ** argv );
extern int main_wimgt ( int argc, char ** argv );
extern int main_wkclt ( int argc, char ** argv );
extern int main_wkmpt ( int argc, char ** argv );
extern int main_wlect ( int argc, char ** argv );
extern int main_wmdlt ( int argc, char ** argv );
extern int main_wpatt ( int argc, char ** argv );
extern int main_wstrt ( int argc, char ** argv );
extern int main_wrapper ( int argc, char ** argv );
extern int main_getopt ( int argc, char ** argv );

// [[wrapper_t]]
typedef struct wrapper_t
{
    uint	hide;	// 0:off, 1:hide, 2:is wrapper
    main_func	func;
    ccp		name;
    ccp		info;
}
wrapper_t;

static const wrapper_t wrapper_tab[] =
{
    { 0, main_wszst, WSZST_SHORT, WSZST_LONG },
    { 0, main_wbmgt, WBMGT_SHORT, WBMGT_LONG },
    { 0, main_wctct, WCTCT_SHORT, WCTCT_LONG },
    { 0, main_wimgt, WIMGT_SHORT, WIMGT_LONG },
    { 0, main_wkclt, WKCLT_SHORT, WKCLT_LONG },
    { 0, main_wkmpt, WKMPT_SHORT, WKMPT_LONG },
    { 0, main_wlect, WLECT_SHORT, WLECT_LONG },
    { 0, main_wmdlt, WMDLT_SHORT, WMDLT_LONG },
    { 0, main_wpatt, WPATT_SHORT, WPATT_LONG },
    { 0, main_wstrt, WSTRT_SHORT, WSTRT_LONG },
    { 2, main_getopt, "getopt", 0 },
    { 2, main_wrapper, "wrapper", 0 },
    {0,0,0}
};

///////////////////////////////////////////////////////////////////////////////

static const wrapper_t * FindWrapper ( int argc, char ** argv )
{
    if ( argc > 0 && argv[0] )
    {
	ccp name = strrchr(argv[0],'/');
	name = name ? name+1 : argv[0];
	if ( *name == 'w' || *name == 'W' )
	    name++;

	const wrapper_t *w;
	for ( w = wrapper_tab; w->func; w++ )
	    if ( !strncasecmp(w->name+1,name,3) )
		return w;
    }

    return wrapper_tab;
}

///////////////////////////////////////////////////////////////////////////////

int main_wrapper ( int argc, char ** argv )
{
    SetupLib(argc,argv,TOOLSET_SHORT,VERSION,TITLE);

    enum
    {
	C_CREATE	= 0x001,
	C_OVERWRITE	= 0x002,

	C_HARDLINKS	= 0x010,
	C_SOFTLINKS	= 0x020,
	C_CYGWIN	= 0x040,
	C_SHELL		= 0x080,
	C_BASH		= 0x100,

	C_QUIET		= 0x200,
	C_VERBOSE	= 0x400,

	C_M_DEST	= C_HARDLINKS|C_SOFTLINKS|C_CYGWIN|C_SHELL,
    };

    static const KeywordTab_t keytab[] =
    {
	{ C_CREATE,		"CREATE",	0, 3 },
	{ C_CREATE|C_OVERWRITE,	"OVERWRITE",	0, 4 },

	{ C_HARDLINKS,		"HARDLINKS",	0, 4 },
    #ifdef __CYGWIN__
	{ C_HARDLINKS,		"BESTLINKS",	0, 4 },
    #endif
	{ C_SOFTLINKS,		"SOFTLINKS",	0, 4 },
    #ifndef __CYGWIN__
	{ C_SOFTLINKS,		"BESTLINKS",	0, 4 },
    #endif
	{ C_CYGWIN,		"CYGWIN",	0, 3 },
	{ C_SHELL,		"SHELL",	0, 2 },
	{ C_SHELL|C_BASH,	"BASH",		0, 4 },

	{ C_QUIET,		"QUIET",	0, 1 },
	{ C_VERBOSE,		"VERBOSE",	0, 1 },

	{0,0,0,0}
    };

    uint i, param = 0;
    for ( i = 1; i < argc; i++ )
    {
	const KeywordTab_t *key = ScanKeyword(0,argv[i],keytab);
	if ( !key || strlen(argv[i]) < key->opt )
	    goto abort;
	param |= key->id;
    }

    const uint quiet	= param & C_QUIET;
    const uint verbose	= param & C_VERBOSE;
    const uint overwrite= param & C_OVERWRITE;
    const uint bash	= param & C_BASH;
    const uint desttype	= param & C_M_DEST;

    char mypath[PATH_MAX], dest[sizeof(mypath)+20];
    GetProgramPath(mypath,sizeof(mypath),true,argv[0]);
    if (verbose)
	printf("My Path: %s\n",mypath);

    if ( param & C_CREATE
	&& Count1Bits(&desttype,sizeof(desttype)) == 1
	&& *mypath )
    {
	memcpy(dest,mypath,sizeof(mypath));
	char *fname = strrchr(dest,'/');
	fname = fname ? fname+1 : dest;
	if (!*fname)
	    goto abort;

	u16 cygwin_link[1000];
	uint cygwin_len = 0;
	if ( desttype == C_CYGWIN )
	{
	    ccp src = fname;
	    while ( cygwin_len < sizeof(cygwin_link)/sizeof(*cygwin_link) )
	    {
		u32 code = ScanUTF8AnsiChar(&src);
		if (!code)
		    break;
		write_le16(cygwin_link+cygwin_len++,code);
	    }
	}

	ccp srcname = mypath + (fname-dest);
	char srcname_buf[PATH_MAX];
	if ( desttype == C_SHELL )
	{
	    ccp src;
	    for ( src = srcname; *src; src++ )
	    {
		const int ch = *src;
		if ( !isalnum(ch) && ch != '_' && ( ch != '-' || src == srcname ) )
		{
		    uint len;
		    PrintEscapedString( srcname_buf+1,
				sizeof(srcname_buf)-2, srcname, -1, CHMD__ALL, '"', &len );
		    if (bash)
			srcname = srcname_buf+1;
		    else
		    {
			srcname_buf[0] = srcname_buf[len] = '"';
			srcname = srcname_buf;
		    }
		    break;
		}
	    }
	}

	FILE *f = 0;
	const wrapper_t *w;

	for ( w = wrapper_tab; w->func; w++ )
	{
	    if (w->hide)
		continue;

	    strcpy(fname,w->name);
	    if (!strcmp(mypath,dest))
		continue;

	    struct stat st;
	    if ( !overwrite && !stat(dest,&st) )
		continue;
	    unlink(dest);

	    switch(desttype)
	    {
	     case C_HARDLINKS:
		if (!quiet)
		    printf("CREATE HARDLINK %s\n",dest);
		link(mypath,dest);
		break;

	     case C_SOFTLINKS:
		if (!quiet)
		    printf("CREATE SOFTLINK %s\n",dest);
		symlink(srcname,dest);
		break;

	     case C_CYGWIN:
		if (!quiet)
		    printf("CREATE CYGWIN SOFTLINK %s\n",dest);
		f = fopen(dest,"w");
		if (f)
		{
		    fwrite("!<symlink>\xff\xfe",1,12,f);
		    fwrite(cygwin_link,2,cygwin_len,f);
		    fwrite("\0\0",1,2,f);
		    fclose(f);
		}
		break;

	     case C_SHELL:
		if (!quiet)
		    printf("CREATE %sSH SCRIPT %s\n", bash ? "BA" : "", dest);
		f = fopen(dest,"w");
		if (f)
		{
		    if (bash)
			fprintf(f,"#!/usr/bin/env bash\n"
				"\"$(dirname \"$BASH_SOURCE\")/%s\" %s \"$@\"\n",
				srcname, w->name );
		    else
			fprintf(f,"#!/bin/sh\n%s %s \"$@\"\n",srcname,w->name);
		    fclose(f);
		    chmod(dest,0755);
		}
		break;
		//"$(dirname "$BASH_SOURCE")"
	    }
	}
	return ERR_OK;
    }

 abort:;
    fprintf(stdout,
	"\n%s\n%s\n\n   Usage: %s TOOLNAME ...\n\n"
	"This is a wrapper for the following tools:\n\n",
	text_logo, TOOLSET_TITLE, ProgInfo.progname );

    const wrapper_t *w, *active = FindWrapper(argc,argv);
    DASSERT(active);
    for ( w = wrapper_tab; w->func; w++ )
	if ( w == active || !w->hide )
	    fprintf(stdout,"  %c %-5s : %s\n",
		w == active ? '*' : ' ', w->name, w->info );

    fprintf(stdout,
	"\nWrapper commands:\n\n"
	"   WRAPPER HELP\n"
	"   WRAPPER CREATE    [QUIET] HARDLINKS|SOFTLINKS|BESTLINKS|CYGWIN|SHELL|BASH\n"
	"   WRAPPER OVERWRITE [QUIET] HARDLINKS|SOFTLINKS|BESTLINKS|CYGWIN|SHELL|BASH\n"
	"\n"
	"   H[ELP]      : Print this help and exit.\n"
	"   CRE[ATE]    : Create links or scripts, but don't overwrite.\n"
	"   OVER[WRITE] : Create links or scripts and remove existing files before.\n"
	"   HARD[LINKS] : Create hard links to the main program.\n"
	"   SOFT[LINKS] : Create soft links to the main program.\n"
	"   BEST[LINKS] : For Cygwin same as HARDLINKS, for all other same as SOFTLINKS.\n"
	"   CYG[WIN]    : Create softlinks for Cygwin (plain files with special content).\n"
	"   SH[ELL]     : Create simple `sh´ scripts assuming the main program is in PATH.\n"
	"   BASH        : Create `bash´ scripts with run time path detection.\n"
	"   Q[UIET]     : Option to suppress creation messages.\n"
	"\n"
	"   See https://szs.wiimm.de/doc/wrapper for details.\n"
	" \n");

    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int main ( int argc, char ** argv )
{
    ArgManager_t am = {0};
    SetupArgManager(&am,LOUP_AUTO,argc,argv,false);
    ExpandAtArgManager(&am,AMXM_SHORT,10,false);
    argc = am.argc;
    argv = am.argv;

    //PRINT("ERR__N=%u\n",ERR__N);
    if ( argc > 1 )
    {
	const wrapper_t *w;
	for ( w = wrapper_tab; w->func; w++ )
	    if (!strcasecmp(w->name,argv[1]))
	    {
		argv[1] = argv[0];
		return w->func(argc-1,argv+1);
	    }
    }

    const wrapper_t *w = FindWrapper(argc,argv);
    DASSERT(w);
    const int err = w->func(argc,argv);
    ClosePager();
    return FixExitStatus(err);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    END				///////////////
///////////////////////////////////////////////////////////////////////////////
