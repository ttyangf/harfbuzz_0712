/*
 * Copyright © 2011  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Google Author(s): Behdad Esfahbod
 */


#include "hb-ot-shape-complex-indic-private.hh"

static int
compare_codepoint (const void *pa, const void *pb)
{
  hb_codepoint_t a = * (hb_codepoint_t *) pa;
  hb_codepoint_t b = * (hb_codepoint_t *) pb;

  return a < b ? -1 : a == b ? 0 : +1;
}

static indic_position_t
consonant_position (hb_codepoint_t u)
{
  consonant_position_t *record;

  record = (consonant_position_t *) bsearch (&u, consonant_positions,
					     ARRAY_LENGTH (consonant_positions),
					     sizeof (consonant_positions[0]),
					     compare_codepoint);

  return record ? record->position : POS_BASE_C;
}

static bool
is_ra (hb_codepoint_t u)
{
  return !!bsearch (&u, ra_chars,
		    ARRAY_LENGTH (ra_chars),
		    sizeof (ra_chars[0]),
		    compare_codepoint);
}

static bool
is_joiner (const hb_glyph_info_t &info)
{
  return !!(FLAG (info.indic_category()) & (FLAG (OT_ZWJ) | FLAG (OT_ZWNJ)));
}

static bool
is_consonant (const hb_glyph_info_t &info)
{
  return !!(FLAG (info.indic_category()) & (FLAG (OT_C) | FLAG (OT_Ra)));
}

static const struct {
  hb_tag_t tag;
  hb_bool_t is_global;
} indic_basic_features[] =
{
  {HB_TAG('n','u','k','t'), true},
  {HB_TAG('a','k','h','n'), false},
  {HB_TAG('r','p','h','f'), false},
  {HB_TAG('r','k','r','f'), true},
  {HB_TAG('p','r','e','f'), false},
  {HB_TAG('b','l','w','f'), false},
  {HB_TAG('h','a','l','f'), false},
  {HB_TAG('v','a','t','u'), true},
  {HB_TAG('p','s','t','f'), false},
  {HB_TAG('c','j','c','t'), false},
};

/* Same order as the indic_basic_features array */
enum {
  _NUKT,
  AKHN,
  RPHF,
  _RKRF,
  PREF,
  BLWF,
  HALF,
  _VATU,
  PSTF,
  CJCT
};

static const hb_tag_t indic_other_features[] =
{
  HB_TAG('p','r','e','s'),
  HB_TAG('a','b','v','s'),
  HB_TAG('b','l','w','s'),
  HB_TAG('p','s','t','s'),
  HB_TAG('h','a','l','n'),

  HB_TAG('d','i','s','t'),
  HB_TAG('a','b','v','m'),
  HB_TAG('b','l','w','m'),
};


static void
initial_reordering (const hb_ot_map_t *map,
		    hb_face_t *face,
		    hb_buffer_t *buffer,
		    void *user_data HB_UNUSED);
static void
final_reordering (const hb_ot_map_t *map,
		  hb_face_t *face,
		  hb_buffer_t *buffer,
		  void *user_data HB_UNUSED);

void
_hb_ot_shape_complex_collect_features_indic (hb_ot_map_builder_t *map, const hb_segment_properties_t  *props)
{
  map->add_bool_feature (HB_TAG('l','o','c','l'));
  /* The Indic specs do not require ccmp, but we apply it here since if
   * there is a use of it, it's typically at the beginning. */
  map->add_bool_feature (HB_TAG('c','c','m','p'));

  map->add_gsub_pause (initial_reordering, NULL);

  for (unsigned int i = 0; i < ARRAY_LENGTH (indic_basic_features); i++) {
    map->add_bool_feature (indic_basic_features[i].tag, indic_basic_features[i].is_global);
    map->add_gsub_pause (NULL, NULL);
  }

  map->add_gsub_pause (final_reordering, NULL);

  for (unsigned int i = 0; i < ARRAY_LENGTH (indic_other_features); i++) {
    map->add_bool_feature (indic_other_features[i], true);
    map->add_gsub_pause (NULL, NULL);
  }
}


hb_ot_shape_normalization_mode_t
_hb_ot_shape_complex_normalization_preference_indic (void)
{
  /* We want split matras decomposed by the common shaping logic. */
  return HB_OT_SHAPE_NORMALIZATION_MODE_DECOMPOSED;
}


void
_hb_ot_shape_complex_setup_masks_indic (hb_ot_map_t *map, hb_buffer_t *buffer, hb_font_t *font)
{
  HB_BUFFER_ALLOCATE_VAR (buffer, indic_category);
  HB_BUFFER_ALLOCATE_VAR (buffer, indic_position);

  /* We cannot setup masks here.  We save information about characters
   * and setup masks later on in a pause-callback. */

  unsigned int count = buffer->len;
  for (unsigned int i = 0; i < count; i++)
  {
    hb_glyph_info_t &info = buffer->info[i];
    unsigned int type = get_indic_categories (info.codepoint);

    info.indic_category() = type & 0x0F;
    info.indic_position() = type >> 4;

    if (info.indic_category() == OT_C) {
      info.indic_position() = consonant_position (info.codepoint);
      if (is_ra (info.codepoint))
	info.indic_category() = OT_Ra;
    } else if (info.indic_category() == OT_SM ||
	       info.indic_category() == OT_VD) {
      info.indic_position() = POS_SMVD;
    } else if (unlikely (info.codepoint == 0x200C))
      info.indic_category() = OT_ZWNJ;
    else if (unlikely (info.codepoint == 0x200D))
      info.indic_category() = OT_ZWJ;

    if (unlikely (info.codepoint == 0x0952)) {
      info.indic_category() = OT_A;
      info.indic_position() = POS_SMVD;
    }
  }
}

static int
compare_indic_order (const hb_glyph_info_t *pa, const hb_glyph_info_t *pb)
{
  int a = pa->indic_position();
  int b = pb->indic_position();

  return a < b ? -1 : a == b ? 0 : +1;
}

static void
found_consonant_syllable (const hb_ot_map_t *map, hb_buffer_t *buffer, hb_mask_t *mask_array,
			  unsigned int start, unsigned int end)
{
  unsigned int i;
  hb_glyph_info_t *info = buffer->info;

  /* Comments from:
   * https://www.microsoft.com/typography/otfntdev/devanot/shaping.aspx */

  /* 1. Find base consonant:
   *
   * The shaping engine finds the base consonant of the syllable, using the
   * following algorithm: starting from the end of the syllable, move backwards
   * until a consonant is found that does not have a below-base or post-base
   * form (post-base forms have to follow below-base forms), or that is not a
   * pre-base reordering Ra, or arrive at the first consonant. The consonant
   * stopped at will be the base.
   *
   *   o If the syllable starts with Ra + Halant (in a script that has Reph)
   *     and has more than one consonant, Ra is excluded from candidates for
   *     base consonants.
   */

  unsigned int base = end;
  bool has_reph = false;

  /* -> If the syllable starts with Ra + Halant (in a script that has Reph)
   *    and has more than one consonant, Ra is excluded from candidates for
   *    base consonants. */
  unsigned int limit = start;
  if (mask_array[RPHF] &&
      start + 2 < end &&
      info[start].indic_category() == OT_Ra &&
      info[start + 1].indic_category() == OT_H)
  {
    limit += 2;
    base = start;
    has_reph = true;
  };

  /* -> starting from the end of the syllable, move backwards */
  i = end;
  do {
    i--;
    /* -> until a consonant is found */
    if (is_consonant (info[i]))
    {
      /* -> that does not have a below-base or post-base form
       * (post-base forms have to follow below-base forms), */
      if (info[i].indic_position() != POS_BELOW_C &&
	  info[i].indic_position() != POS_POST_C)
      {
        base = i;
	break;
      }

      /* -> or that is not a pre-base reordering Ra,
       *
       * TODO
       */

      /* ->  o If the syllable starts with Ra + Halant (in a script that has Reph)
       *       and has more than one consonant, Ra is excluded from candidates for
       *       base consonants.
       *
       * IMPLEMENTATION NOTES:
       *
       * We do this by adjusting limit accordingly before entering the loop.
       */

      /* -> or arrive at the first consonant. The consonant stopped at will
       * be the base. */
      base = i;
    }
    else
      if (is_joiner (info[i]))
        break;
  } while (i > limit);
  if (base < start)
    base = start; /* Just in case... */


  /* 2. Decompose and reorder Matras:
   *
   * Each matra and any syllable modifier sign in the cluster are moved to the
   * appropriate position relative to the consonant(s) in the cluster. The
   * shaping engine decomposes two- or three-part matras into their constituent
   * parts before any repositioning. Matra characters are classified by which
   * consonant in a conjunct they have affinity for and are reordered to the
   * following positions:
   *
   *   o Before first half form in the syllable
   *   o After subjoined consonants
   *   o After post-form consonant
   *   o After main consonant (for above marks)
   *
   * IMPLEMENTATION NOTES:
   *
   * The normalize() routine has already decomposed matras for us, so we don't
   * need to worry about that.
   */


  /* 3.  Reorder marks to canonical order:
   *
   * Adjacent nukta and halant or nukta and vedic sign are always repositioned
   * if necessary, so that the nukta is first.
   *
   * IMPLEMENTATION NOTES:
   *
   * We don't need to do this: the normalize() routine already did this for us.
   */


  /* Reorder characters */

  for (i = start; i < base; i++)
    info[i].indic_position() = POS_PRE_C;
  info[base].indic_position() = POS_BASE_C;


  /* Handle beginning Ra */
  if (has_reph &&
      start + 3 <= end &&
      !is_joiner (info[start + 2]))
   {
    info[start].indic_position() = POS_REPH;
    info[start].mask = mask_array[RPHF];
   }

  /* For old-style Indic script tags, move the first post-base Halant after
   * last consonant. */
  if ((map->get_chosen_script (0) & 0x000000FF) != '2') {
    /* We should only do this for Indic scripts which have a version two I guess. */
    for (i = base + 1; i < end; i++)
      if (info[i].indic_category() == OT_H) {
        unsigned int j;
        for (j = end - 1; j > i; j--)
	  if ((FLAG (info[j].indic_category()) & (FLAG (OT_C) | FLAG (OT_Ra))))
	    break;
	if (j > i) {
	  /* Move Halant to after last consonant. */
	  hb_glyph_info_t t = info[i];
	  memmove (&info[i], &info[i + 1], (j - i) * sizeof (info[0]));
	  info[j] = t;
	}
        break;
      }
  }

  /* Attach ZWJ, ZWNJ, nukta, and halant to previous char to move with them. */
  for (i = start + 1; i < end; i++)
    if ((FLAG (info[i].indic_category()) &
	 (FLAG (OT_ZWNJ) | FLAG (OT_ZWJ) | FLAG (OT_N) | FLAG (OT_H))))
      info[i].indic_position() = info[i - 1].indic_position();

  /* We do bubble-sort, skip malicious clusters attempts */
  if (end - start > 20)
    return;

  /* Sit tight, rock 'n roll! */
  hb_bubble_sort (info + start, end - start, compare_indic_order);

  /* Setup masks now */

  {
    hb_mask_t mask;

    /* Pre-base */
    mask = mask_array[HALF] | mask_array[AKHN] | mask_array[CJCT];
    for (i = start; i < base; i++)
      info[i].mask  |= mask;
    /* Base */
    mask = mask_array[AKHN] | mask_array[CJCT];
    info[base].mask |= mask;
    /* Post-base */
    mask = mask_array[BLWF] | mask_array[PSTF] | mask_array[CJCT];
    for (i = base + 1; i < end; i++)
      info[i].mask  |= mask;
  }

  /* Apply ZWJ/ZWNJ effects */
  for (i = start + 1; i < end; i++)
    if (is_joiner (info[i])) {
      bool non_joiner = info[i].indic_category() == OT_ZWNJ;
      unsigned int j = i;

      do {
	j--;

	/* Reading the Unicode and OpenType specs, I think the following line
	 * is correct, but this is not what the test suite expects currently.
	 * The test suite has been drinking, not me...  But disable while
	 * investigating.
	 */
	//info[j].mask &= !mask_array[CJCT];
	if (non_joiner)
	  info[j].mask &= !mask_array[HALF];

      } while (j > start && !is_consonant (info[j]));
    }
}


static void
found_vowel_syllable (const hb_ot_map_t *map, hb_buffer_t *buffer, hb_mask_t *mask_array,
		      unsigned int start, unsigned int end)
{
  /* TODO
   * Not clear to me how this should work.  Do the matras move to before the
   * independent vowel?  No idea.
   */
}

static void
found_standalone_cluster (const hb_ot_map_t *map, hb_buffer_t *buffer, hb_mask_t *mask_array,
			  unsigned int start, unsigned int end)
{
  /* TODO
   * Easiest thing to do here is to convert the NBSP to consonant and
   * call found_consonant_syllable.
   */
}

static void
found_non_indic (const hb_ot_map_t *map, hb_buffer_t *buffer, hb_mask_t *mask_array,
		 unsigned int start, unsigned int end)
{
  /* Nothing to do right now.  If we ever switch to using the output
   * buffer in the reordering process, we'd need to next_glyph() here. */
}

#include "hb-ot-shape-complex-indic-machine.hh"

static void
initial_reordering (const hb_ot_map_t *map,
		    hb_face_t *face,
		    hb_buffer_t *buffer,
		    void *user_data HB_UNUSED)
{
  hb_mask_t mask_array[ARRAY_LENGTH (indic_basic_features)] = {0};
  unsigned int num_masks = ARRAY_LENGTH (indic_basic_features);
  for (unsigned int i = 0; i < num_masks; i++)
    mask_array[i] = map->get_1_mask (indic_basic_features[i].tag);

  find_syllables (map, buffer, mask_array);
}

static void
final_reordering (const hb_ot_map_t *map,
		  hb_face_t *face,
		  hb_buffer_t *buffer,
		  void *user_data HB_UNUSED)
{
  /* 4. Final reordering:
   *
   * After the localized forms and basic shaping forms GSUB features have been
   * applied (see below), the shaping engine performs some final glyph
   * reordering before applying all the remaining font features to the entire
   * cluster.
   *
   *   o Reorder matras:
   *
   *     If a pre-base matra character had been reordered before applying basic
   *     features, the glyph can be moved closer to the main consonant based on
   *     whether half-forms had been formed. Actual position for the matra is
   *     defined as “after last standalone halant glyph, after initial matra
   *     position and before the main consonant”. If ZWJ or ZWNJ follow this
   *     halant, position is moved after it.
   *
   *   o Reorder reph:
   *
   *     Reph’s original position is always at the beginning of the syllable,
   *     (i.e. it is not reordered at the character reordering stage). However,
   *     it will be reordered according to the basic-forms shaping results.
   *     Possible positions for reph, depending on the script, are; after main,
   *     before post-base consonant forms, and after post-base consonant forms.
   *
   *       1. If reph should be positioned after post-base consonant forms,
   *          proceed to step 5.
   *
   *       2. If the reph repositioning class is not after post-base: target
   *          position is after the first explicit halant glyph between the
   *          first post-reph consonant and last main consonant. If ZWJ or ZWNJ
   *          are following this halant, position is moved after it. If such
   *          position is found, this is the target position. Otherwise,
   *          proceed to the next step.
   *
   *          Note: in old-implementation fonts, where classifications were
   *          fixed in shaping engine, there was no case where reph position
   *          will be found on this step.
   *
   *       3. If reph should be repositioned after the main consonant: from the
   *          first consonant not ligated with main, or find the first
   *          consonant that is not a potential pre-base reordering Ra.
   *
   *
   *       4. If reph should be positioned before post-base consonant, find
   *          first post-base classified consonant not ligated with main. If no
   *          consonant is found, the target position should be before the
   *          first matra, syllable modifier sign or vedic sign.
   *
   *       5. If no consonant is found in steps 3 or 4, move reph to a position
   *          immediately before the first post-base matra, syllable modifier
   *          sign or vedic sign that has a reordering class after the intended
   *          reph position. For example, if the reordering position for reph
   *          is post-main, it will skip above-base matras that also have a
   *          post-main position.
   *
   *       6. Otherwise, reorder reph to the end of the syllable.
   *
   *   o Reorder pre-base reordering consonants:
   *
   *     If a pre-base reordering consonant is found, reorder it according to
   *     the following rules:
   *
   *       1. Only reorder a glyph produced by substitution during application
   *          of the feature. (Note that a font may shape a Ra consonant with
   *          the feature generally but block it in certain contexts.)
   *
   *       2. Try to find a target position the same way as for pre-base matra.
   *          If it is found, reorder pre-base consonant glyph.
   *
   *       3. If position is not found, reorder immediately before main
   *          consonant.
   */

  /* TODO */



  HB_BUFFER_DEALLOCATE_VAR (buffer, indic_category);
  HB_BUFFER_DEALLOCATE_VAR (buffer, indic_position);
}



