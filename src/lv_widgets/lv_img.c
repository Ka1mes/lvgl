/**
 * @file lv_img.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_img.h"
#if LV_USE_IMG != 0

/*Testing of dependencies*/
#if LV_USE_LABEL == 0
    #error "lv_img: lv_label is required. Enable it in lv_conf.h (LV_USE_LABEL  1) "
#endif

#include "../lv_misc/lv_debug.h"
#include "../lv_themes/lv_theme.h"
#include "../lv_draw/lv_img_decoder.h"
#include "../lv_misc/lv_fs.h"
#include "../lv_misc/lv_txt.h"
#include "../lv_misc/lv_math.h"
#include "../lv_misc/lv_log.h"

/*********************
 *      DEFINES
 *********************/
#define LV_OBJX_NAME "lv_img"

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_design_res_t lv_img_design(lv_obj_t * img, const lv_area_t * clip_area, lv_design_mode_t mode);
static lv_res_t lv_img_signal(lv_obj_t * img, lv_signal_t sign, void * param);
static lv_style_list_t * lv_img_get_style(lv_obj_t * img, uint8_t type);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_signal_cb_t ancestor_signal;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Create an image objects
 * @param par pointer to an object, it will be the parent of the new button
 * @param copy pointer to a image object, if not NULL then the new object will be copied from it
 * @return pointer to the created image
 */
lv_obj_t * lv_img_create(lv_obj_t * par, const lv_obj_t * copy)
{
    LV_LOG_TRACE("image create started");

    /*Create a basic object*/
    lv_obj_t * img = lv_obj_create(par, copy);
    LV_ASSERT_MEM(img);
    if(img == NULL) return NULL;

    if(ancestor_signal == NULL) ancestor_signal = lv_obj_get_signal_cb(img);

    /*Extend the basic object to image object*/
    lv_img_ext_t * ext = lv_obj_allocate_ext_attr(img, sizeof(lv_img_ext_t));
    LV_ASSERT_MEM(ext);
    if(ext == NULL) {
        lv_obj_del(img);
        return NULL;
    }

    ext->src       = NULL;
    ext->src_type  = LV_IMG_SRC_UNKNOWN;
    ext->cf        = LV_IMG_CF_UNKNOWN;
    ext->w         = lv_obj_get_width(img);
    ext->h         = lv_obj_get_height(img);
    ext->angle = 0;
    ext->zoom = LV_IMG_ZOOM_NONE;
    ext->antialias = LV_ANTIALIAS ? 1 : 0;
    ext->offset.x  = 0;
    ext->offset.y  = 0;
    ext->pivot.x = 0;
    ext->pivot.y = 0;

    /*Init the new object*/
    lv_obj_set_signal_cb(img, lv_img_signal);
    lv_obj_set_design_cb(img, lv_img_design);

    if(copy == NULL) {
        lv_theme_apply(img, LV_THEME_IMAGE);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(img, LV_OBJ_FLAG_ADV_HITTEST);

        /* Enable auto size for non screens
         * because image screens are wallpapers
         * and must be screen sized*/
        if(par) lv_obj_set_size(img, LV_SIZE_AUTO, LV_SIZE_AUTO);
    }
    else {
        lv_img_ext_t * copy_ext = lv_obj_get_ext_attr(copy);
        ext->zoom      = copy_ext->zoom;
        ext->angle     = copy_ext->angle;
        ext->antialias = copy_ext->antialias;
        ext->offset  = copy_ext->offset;
        ext->pivot   = copy_ext->pivot;
        lv_img_set_src(img, copy_ext->src);

        /*Refresh the style with new signal function*/
        _lv_obj_refresh_style(img, LV_OBJ_PART_ALL, LV_STYLE_PROP_ALL);
    }

    LV_LOG_INFO("image created");

    return img;
}

/*=====================
 * Setter functions
 *====================*/

/**
 * Set the pixel map to display by the image
 * @param img pointer to an image object
 * @param data the image data
 */
void lv_img_set_src(lv_obj_t * img, const void * src_img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_obj_invalidate(img);

    lv_img_src_t src_type = lv_img_src_get_type(src_img);
    lv_img_ext_t * ext    = lv_obj_get_ext_attr(img);

#if LV_USE_LOG && LV_LOG_LEVEL >= LV_LOG_LEVEL_INFO
    switch(src_type) {
        case LV_IMG_SRC_FILE:
            LV_LOG_TRACE("lv_img_set_src: `LV_IMG_SRC_FILE` type found");
            break;
        case LV_IMG_SRC_VARIABLE:
            LV_LOG_TRACE("lv_img_set_src: `LV_IMG_SRC_VARIABLE` type found");
            break;
        case LV_IMG_SRC_SYMBOL:
            LV_LOG_TRACE("lv_img_set_src: `LV_IMG_SRC_SYMBOL` type found");
            break;
        default:
            LV_LOG_WARN("lv_img_set_src: unknown type");
    }
#endif

    /*If the new source type is unknown free the memories of the old source*/
    if(src_type == LV_IMG_SRC_UNKNOWN) {
        LV_LOG_WARN("lv_img_set_src: unknown image type");
        if(ext->src_type == LV_IMG_SRC_SYMBOL || ext->src_type == LV_IMG_SRC_FILE) {
            lv_mem_free(ext->src);
        }
        ext->src      = NULL;
        ext->src_type = LV_IMG_SRC_UNKNOWN;
        return;
    }

    lv_img_header_t header;
    lv_img_decoder_get_info(src_img, &header);

    /*Save the source*/
    if(src_type == LV_IMG_SRC_VARIABLE) {
        LV_LOG_INFO("lv_img_set_src:  `LV_IMG_SRC_VARIABLE` type found");

        /*If memory was allocated because of the previous `src_type` then free it*/
        if(ext->src_type == LV_IMG_SRC_FILE || ext->src_type == LV_IMG_SRC_SYMBOL) {
            lv_mem_free(ext->src);
        }
        ext->src = src_img;
    }
    else if(src_type == LV_IMG_SRC_FILE || src_type == LV_IMG_SRC_SYMBOL) {
        /* If the new and the old src are the same then it was only a refresh.*/
        if(ext->src != src_img) {
            const void * old_src = NULL;
            /* If memory was allocated because of the previous `src_type` then save its pointer and free after allocation.
             * It's important to allocate first to be sure the new data will be on a new address.
             * Else `img_cache` wouldn't see the change in source.*/
            if(ext->src_type == LV_IMG_SRC_FILE || ext->src_type == LV_IMG_SRC_SYMBOL) {
                old_src = ext->src;
            }
            char * new_str = lv_mem_alloc(strlen(src_img) + 1);
            LV_ASSERT_MEM(new_str);
            if(new_str == NULL) return;
            strcpy(new_str, src_img);
            ext->src = new_str;

            if(old_src) lv_mem_free(old_src);
        }
    }

    if(src_type == LV_IMG_SRC_SYMBOL) {
        /*`lv_img_dsc_get_info` couldn't set the with and height of a font so set it here*/
        const lv_font_t * font = lv_obj_get_style_text_font(img, LV_IMG_PART_MAIN);
        lv_coord_t letter_space = lv_obj_get_style_text_letter_space(img, LV_IMG_PART_MAIN);
        lv_coord_t line_space = lv_obj_get_style_text_line_space(img, LV_IMG_PART_MAIN);
        lv_point_t size;
        _lv_txt_get_size(&size, src_img, font, letter_space, line_space,
                         LV_COORD_MAX, LV_TXT_FLAG_NONE);
        header.w = size.x;
        header.h = size.y;
    }

    ext->src_type = src_type;
    ext->w        = header.w;
    ext->h        = header.h;
    ext->cf       = header.cf;
    ext->pivot.x = header.w / 2;
    ext->pivot.y = header.h / 2;

    _lv_obj_handle_self_size_chg(img);

    /*Provide enough room for the rotated corners*/
    if(ext->angle || ext->zoom != LV_IMG_ZOOM_NONE) _lv_obj_refresh_ext_draw_pad(img);

    lv_obj_invalidate(img);
}

/**
 * Set an offset for the source of an image.
 * so the image will be displayed from the new origin.
 * @param img pointer to an image
 * @param x: the new offset along x axis.
 */
void lv_img_set_offset_x(lv_obj_t * img, lv_coord_t x)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    x = x % ext->w;

    ext->offset.x = x;
    lv_obj_invalidate(img);
}

/**
 * Set an offset for the source of an image.
 * so the image will be displayed from the new origin.
 * @param img pointer to an image
 * @param y: the new offset along y axis.
 */
void lv_img_set_offset_y(lv_obj_t * img, lv_coord_t y)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    y = y % ext->h;

    ext->offset.y = y;
    lv_obj_invalidate(img);
}

/**
 * Set the rotation center of the image.
 * The image will be rotated around this point
 * @param img pointer to an image object
 * @param x rotation center x of the image
 * @param y rotation center y of the image
 */
void lv_img_set_pivot(lv_obj_t * img, lv_coord_t x, lv_coord_t y)
{
    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);
    if(ext->pivot.x == x && ext->pivot.y == y) return;

    lv_coord_t transf_zoom = lv_obj_get_style_transform_zoom(img, LV_IMG_PART_MAIN);
    transf_zoom = (transf_zoom * ext->zoom) >> 8;

    lv_coord_t transf_angle = lv_obj_get_style_transform_angle(img, LV_IMG_PART_MAIN);
    transf_angle += ext->angle;

    lv_coord_t w = lv_obj_get_width(img);
    lv_coord_t h = lv_obj_get_height(img);
    lv_area_t a;
    _lv_img_buf_get_transformed_area(&a, w, h, transf_angle, transf_zoom, &ext->pivot);
    a.x1 += img->coords.x1;
    a.y1 += img->coords.y1;
    a.x2 += img->coords.x1;
    a.y2 += img->coords.y1;
    lv_obj_invalidate_area(img, &a);

    ext->pivot.x = x;
    ext->pivot.y = y;
    _lv_obj_refresh_ext_draw_pad(img);

    _lv_img_buf_get_transformed_area(&a, w, h, transf_angle, transf_zoom, &ext->pivot);
    a.x1 += img->coords.x1;
    a.y1 += img->coords.y1;
    a.x2 += img->coords.x1;
    a.y2 += img->coords.y1;
    lv_obj_invalidate_area(img, &a);
}

/**
 * Set the rotation angle of the image.
 * The image will be rotated around the set pivot set by `lv_img_set_pivot()`
 * @param img pointer to an image object
 * @param angle rotation angle in degree with 0.1 degree resolution (0..3600: clock wise)
 */
void lv_img_set_angle(lv_obj_t * img, int16_t angle)
{
    if(angle < 0 || angle >= 3600) angle = angle % 3600;

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);
    if(angle == ext->angle) return;

    lv_coord_t transf_zoom = lv_obj_get_style_transform_zoom(img, LV_IMG_PART_MAIN);
    transf_zoom = (transf_zoom * ext->zoom) >> 8;

    lv_coord_t transf_angle = lv_obj_get_style_transform_angle(img, LV_IMG_PART_MAIN);

    lv_coord_t w = lv_obj_get_width(img);
    lv_coord_t h = lv_obj_get_height(img);
    lv_area_t a;
    _lv_img_buf_get_transformed_area(&a, w, h, transf_angle + ext->angle, transf_zoom, &ext->pivot);
    a.x1 += img->coords.x1;
    a.y1 += img->coords.y1;
    a.x2 += img->coords.x1;
    a.y2 += img->coords.y1;
    lv_obj_invalidate_area(img, &a);

    ext->angle = angle;
    _lv_obj_refresh_ext_draw_pad(img);

    _lv_img_buf_get_transformed_area(&a, w, h, transf_angle + ext->angle, transf_zoom, &ext->pivot);
    a.x1 += img->coords.x1;
    a.y1 += img->coords.y1;
    a.x2 += img->coords.x1;
    a.y2 += img->coords.y1;
    lv_obj_invalidate_area(img, &a);
}

/**
 * Set the zoom factor of the image.
 * @param img pointer to an image object
 * @param zoom the zoom factor.
 * - 256 or LV_ZOOM_IMG_NONE for no zoom
 * - <256: scale down
 * - >256 scale up
 * - 128 half size
 * - 512 double size
 */
void lv_img_set_zoom(lv_obj_t * img, uint16_t zoom)
{
    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);
    if(zoom == ext->zoom) return;

    if(zoom == 0) zoom = 1;

    lv_coord_t transf_zoom = lv_obj_get_style_transform_zoom(img, LV_IMG_PART_MAIN);

    lv_coord_t transf_angle = lv_obj_get_style_transform_angle(img, LV_IMG_PART_MAIN);
    transf_angle += ext->angle;

    lv_coord_t w = lv_obj_get_width(img);
    lv_coord_t h = lv_obj_get_height(img);
    lv_area_t a;
    _lv_img_buf_get_transformed_area(&a, w, h, transf_angle, (transf_zoom * ext->zoom) >> 8, &ext->pivot);
    a.x1 += img->coords.x1 - 1;
    a.y1 += img->coords.y1 - 1;
    a.x2 += img->coords.x1 + 1;
    a.y2 += img->coords.y1 + 1;
    lv_obj_invalidate_area(img, &a);

    ext->zoom = zoom;
    _lv_obj_refresh_ext_draw_pad(img);

    _lv_img_buf_get_transformed_area(&a, w, h, transf_angle, (transf_zoom * ext->zoom) >> 8, &ext->pivot);
    a.x1 += img->coords.x1 - 1;
    a.y1 += img->coords.y1 - 1;
    a.x2 += img->coords.x1 + 1;
    a.y2 += img->coords.y1 + 1;
    lv_obj_invalidate_area(img, &a);
}

/**
 * Enable/disable anti-aliasing for the transformations (rotate, zoom) or not
 * @param img pointer to an image object
 * @param antialias true: anti-aliased; false: not anti-aliased
 */
void lv_img_set_antialias(lv_obj_t * img, bool antialias)
{
    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);
    if(antialias == ext->antialias) return;

    ext->antialias = antialias;
    lv_obj_invalidate(img);
}

/*=====================
 * Getter functions
 *====================*/

/**
 * Get the source of the image
 * @param img pointer to an image object
 * @return the image source (symbol, file name or C array)
 */
const void * lv_img_get_src(lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->src;
}

/**
 * Get the offset.x attribute of the img object.
 * @param img pointer to an image
 * @return offset.x value.
 */
lv_coord_t lv_img_get_offset_x(lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->offset.x;
}

/**
 * Get the offset.y attribute of the img object.
 * @param img pointer to an image
 * @return offset.y value.
 */
lv_coord_t lv_img_get_offset_y(lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->offset.y;
}

/**
 * Get the rotation center of the image.
 * @param img pointer to an image object
 * @param center rotation center of the image
 */
void lv_img_get_pivot(lv_obj_t * img, lv_point_t * pivot)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    *pivot = ext->pivot;
}

/**
 * Get the rotation angle of the image.
 * @param img pointer to an image object
 * @return rotation angle in degree (0..359)
 */
uint16_t lv_img_get_angle(lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->angle;
}

/**
 * Get the zoom factor of the image.
 * @param img pointer to an image object
 * @return zoom factor (256: no zoom)
 */
uint16_t lv_img_get_zoom(lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->zoom;
}

/**
 * Get whether the transformations (rotate, zoom) are anti-aliased or not
 * @param img pointer to an image object
 * @return true: anti-aliased; false: not anti-aliased
 */
bool lv_img_get_antialias(lv_obj_t * img)
{
    LV_ASSERT_OBJ(img, LV_OBJX_NAME);

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    return ext->antialias ? true : false;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Handle the drawing related tasks of the images
 * @param img pointer to an object
 * @param clip_area the object will be drawn only in this area
 * @param mode LV_DESIGN_COVER_CHK: only check if the object fully covers the 'mask_p' area
 *                                  (return 'true' if yes)
 *             LV_DESIGN_DRAW: draw the object (always return 'true')
 *             LV_DESIGN_DRAW_POST: drawing after every children are drawn
 * @param return an element of `lv_design_res_t`
 */
static lv_design_res_t lv_img_design(lv_obj_t * img, const lv_area_t * clip_area, lv_design_mode_t mode)
{
    lv_img_ext_t * ext       = lv_obj_get_ext_attr(img);

    if(mode == LV_DESIGN_COVER_CHK) {
        if(lv_obj_get_style_clip_corner(img, LV_IMG_PART_MAIN)) return LV_DESIGN_RES_MASKED;

        if(ext->src_type == LV_IMG_SRC_UNKNOWN || ext->src_type == LV_IMG_SRC_SYMBOL) return LV_DESIGN_RES_NOT_COVER;

        /*Non true color format might have "holes"*/
        if(ext->cf != LV_IMG_CF_TRUE_COLOR && ext->cf != LV_IMG_CF_RAW) return LV_DESIGN_RES_NOT_COVER;

        /*With not LV_OPA_COVER images acn't cover an area */
        if(lv_obj_get_style_image_opa(img, LV_IMG_PART_MAIN) != LV_OPA_COVER) return LV_DESIGN_RES_NOT_COVER;

        int32_t angle_final = lv_obj_get_style_transform_angle(img, LV_IMG_PART_MAIN);
        angle_final += ext->angle;

        if(angle_final != 0) return LV_DESIGN_RES_NOT_COVER;

        int32_t zoom_final = lv_obj_get_style_transform_zoom(img, LV_IMG_PART_MAIN);
        zoom_final = (zoom_final * ext->zoom) >> 8;

        if(zoom_final == LV_IMG_ZOOM_NONE) {
            if(_lv_area_is_in(clip_area, &img->coords, 0) == false) return LV_DESIGN_RES_NOT_COVER;
        }
        else {
            lv_area_t a;
            _lv_img_buf_get_transformed_area(&a, lv_obj_get_width(img), lv_obj_get_height(img), 0, zoom_final, &ext->pivot);
            a.x1 += img->coords.x1;
            a.y1 += img->coords.y1;
            a.x2 += img->coords.x1;
            a.y2 += img->coords.y1;

            if(_lv_area_is_in(clip_area, &a, 0) == false) return LV_DESIGN_RES_NOT_COVER;
        }

#if LV_USE_BLEND_MODES
        if(lv_obj_get_style_bg_blend_mode(img, LV_IMG_PART_MAIN) != LV_BLEND_MODE_NORMAL) return LV_DESIGN_RES_NOT_COVER;
        if(lv_obj_get_style_image_blend_mode(img, LV_IMG_PART_MAIN) != LV_BLEND_MODE_NORMAL) return LV_DESIGN_RES_NOT_COVER;
#endif

        return LV_DESIGN_RES_COVER;
    }
    else if(mode == LV_DESIGN_DRAW_MAIN) {
        if(ext->h == 0 || ext->w == 0) return true;

        lv_draw_rect_dsc_t bg_dsc;
        lv_draw_rect_dsc_init(&bg_dsc);
        lv_obj_init_draw_rect_dsc(img, LV_IMG_PART_MAIN, &bg_dsc);

        /*If the border is drawn later disable loading its properties*/
        if(lv_obj_get_style_border_post(img, LV_OBJ_PART_MAIN)) {
            bg_dsc.border_post = 1;
        }


        int32_t zoom_final = lv_obj_get_style_transform_zoom(img, LV_IMG_PART_MAIN);
        zoom_final = (zoom_final * ext->zoom) >> 8;

        int32_t angle_final = lv_obj_get_style_transform_angle(img, LV_IMG_PART_MAIN);
        angle_final += ext->angle;

        lv_coord_t obj_w = lv_obj_get_width(img);
        lv_coord_t obj_h = lv_obj_get_height(img);

        lv_area_t bg_coords;
        _lv_img_buf_get_transformed_area(&bg_coords, obj_w, obj_h,
                                         angle_final, zoom_final, &ext->pivot);
        bg_coords.x1 += img->coords.x1;
        bg_coords.y1 += img->coords.y1;
        bg_coords.x2 += img->coords.x1;
        bg_coords.y2 += img->coords.y1;
        bg_coords.x1 -= lv_obj_get_style_pad_left(img, LV_IMG_PART_MAIN);
        bg_coords.x2 += lv_obj_get_style_pad_right(img, LV_IMG_PART_MAIN);
        bg_coords.y1 -= lv_obj_get_style_pad_top(img, LV_IMG_PART_MAIN);
        bg_coords.y2 += lv_obj_get_style_pad_bottom(img, LV_IMG_PART_MAIN);

        lv_draw_rect(&bg_coords, clip_area, &bg_dsc);

        if(zoom_final == 0) return LV_DESIGN_RES_OK;

        if(lv_obj_get_style_clip_corner(img, LV_OBJ_PART_MAIN)) {
            lv_draw_mask_radius_param_t * mp = _lv_mem_buf_get(sizeof(lv_draw_mask_radius_param_t));

            lv_coord_t r = lv_obj_get_style_radius(img, LV_OBJ_PART_MAIN);

            lv_draw_mask_radius_init(mp, &bg_coords, r, false);
            /*Add the mask and use `img+8` as custom id. Don't use `obj` directly because it might be used by the user*/
            lv_draw_mask_add(mp, img + 8);
        }

        if(ext->src_type == LV_IMG_SRC_FILE || ext->src_type == LV_IMG_SRC_VARIABLE) {
            LV_LOG_TRACE("lv_img_design: start to draw image");

            lv_draw_img_dsc_t img_dsc;
            lv_draw_img_dsc_init(&img_dsc);
            lv_obj_init_draw_img_dsc(img, LV_IMG_PART_MAIN, &img_dsc);

            img_dsc.zoom = zoom_final;

            if(img_dsc.zoom == 0) return LV_DESIGN_RES_OK;

            img_dsc.angle = angle_final;

            img_dsc.pivot.x = ext->pivot.x;
            img_dsc.pivot.y = ext->pivot.y;
            img_dsc.antialias = ext->antialias;

            lv_coord_t zoomed_src_w = (int32_t)((int32_t)ext->w * zoom_final) >> 8;
            if(zoomed_src_w <= 0) return LV_DESIGN_RES_OK;
            lv_coord_t zoomed_src_h = (int32_t)((int32_t)ext->h * zoom_final) >> 8;
            if(zoomed_src_h <= 0) return LV_DESIGN_RES_OK;
            lv_area_t zoomed_coords;
            lv_obj_get_coords(img, &zoomed_coords);

            zoomed_coords.x1 += (int32_t)((int32_t)ext->offset.x * zoom_final) >> 8;
            zoomed_coords.y1 += (int32_t)((int32_t)ext->offset.y * zoom_final) >> 8;
            zoomed_coords.x2 = zoomed_coords.x1 + ((int32_t)((int32_t)(obj_w - 1) * zoom_final) >> 8);
            zoomed_coords.y2 = zoomed_coords.y1 + ((int32_t)((int32_t)(obj_h - 1) * zoom_final) >> 8);

            if(zoomed_coords.x1 > img->coords.x1) zoomed_coords.x1 -= ext->w;
            if(zoomed_coords.y1 > img->coords.y1) zoomed_coords.y1 -= ext->h;

            lv_area_t clip_real;
            _lv_img_buf_get_transformed_area(&clip_real, lv_obj_get_width(img), lv_obj_get_height(img), angle_final, zoom_final,
                                             &ext->pivot);
            clip_real.x1 += img->coords.x1;
            clip_real.x2 += img->coords.x1;
            clip_real.y1 += img->coords.y1;
            clip_real.y2 += img->coords.y1;

            if(_lv_area_intersect(&clip_real, &clip_real, clip_area) == false) return LV_DESIGN_RES_OK;

            lv_area_t coords_tmp;
            coords_tmp.y1 = zoomed_coords.y1;
            coords_tmp.y2 = zoomed_coords.y1 + ext->h - 1;

            for(; coords_tmp.y1 < zoomed_coords.y2; coords_tmp.y1 += zoomed_src_h, coords_tmp.y2 += zoomed_src_h) {
                coords_tmp.x1 = zoomed_coords.x1;
                coords_tmp.x2 = zoomed_coords.x1 + ext->w - 1;
                for(; coords_tmp.x1 < zoomed_coords.x2; coords_tmp.x1 += zoomed_src_w, coords_tmp.x2 += zoomed_src_w) {
                    lv_draw_img(&coords_tmp, &clip_real, ext->src, &img_dsc);
                }
            }
        }
        else if(ext->src_type == LV_IMG_SRC_SYMBOL) {
            LV_LOG_TRACE("lv_img_design: start to draw symbol");
            lv_draw_label_dsc_t label_dsc;
            lv_draw_label_dsc_init(&label_dsc);
            lv_obj_init_draw_label_dsc(img, LV_IMG_PART_MAIN, &label_dsc);

            label_dsc.color = lv_obj_get_style_image_recolor(img, LV_IMG_PART_MAIN);
            lv_draw_label(&img->coords, clip_area, &label_dsc, ext->src, NULL);
        }
        else {
            /*Trigger the error handler of image drawer*/
            LV_LOG_WARN("lv_img_design: image source type is unknown");
            lv_draw_img(&img->coords, clip_area, NULL, NULL);
        }
    }
    else if(mode == LV_DESIGN_DRAW_POST) {
        if(lv_obj_get_style_clip_corner(img, LV_OBJ_PART_MAIN)) {
            lv_draw_mask_radius_param_t * param = lv_draw_mask_remove_custom(img + 8);
            _lv_mem_buf_release(param);
        }

        /*If the border is drawn later disable loading other properties*/
        if(lv_obj_get_style_border_post(img, LV_OBJ_PART_MAIN)) {
            lv_draw_rect_dsc_t draw_dsc;
            lv_draw_rect_dsc_init(&draw_dsc);
            draw_dsc.bg_opa = LV_OPA_TRANSP;
            draw_dsc.pattern_opa = LV_OPA_TRANSP;
            draw_dsc.shadow_opa = LV_OPA_TRANSP;
            lv_obj_init_draw_rect_dsc(img, LV_OBJ_PART_MAIN, &draw_dsc);

            int32_t zoom_final = lv_obj_get_style_transform_zoom(img, LV_IMG_PART_MAIN);
            zoom_final = (zoom_final * ext->zoom) >> 8;

            int32_t angle_final = lv_obj_get_style_transform_angle(img, LV_IMG_PART_MAIN);
            angle_final += ext->angle;

            lv_area_t bg_coords;
            _lv_img_buf_get_transformed_area(&bg_coords, lv_area_get_width(&img->coords), lv_area_get_height(&img->coords),
                                             angle_final, zoom_final, &ext->pivot);
            bg_coords.x1 += img->coords.x1;
            bg_coords.y1 += img->coords.y1;
            bg_coords.x2 += img->coords.x1;
            bg_coords.y2 += img->coords.y1;
            bg_coords.x1 -= lv_obj_get_style_pad_left(img, LV_IMG_PART_MAIN);
            bg_coords.x2 += lv_obj_get_style_pad_right(img, LV_IMG_PART_MAIN);
            bg_coords.y1 -= lv_obj_get_style_pad_top(img, LV_IMG_PART_MAIN);
            bg_coords.y2 += lv_obj_get_style_pad_bottom(img, LV_IMG_PART_MAIN);

            lv_draw_rect(&img->coords, clip_area, &draw_dsc);
        }
    }

    return LV_DESIGN_RES_OK;
}

/**
 * Signal function of the image
 * @param img pointer to an image object
 * @param sign a signal type from lv_signal_t enum
 * @param param pointer to a signal specific variable
 * @return LV_RES_OK: the object is not deleted in the function; LV_RES_INV: the object is deleted
 */
static lv_res_t lv_img_signal(lv_obj_t * img, lv_signal_t sign, void * param)
{
    lv_res_t res;

    /* Include the ancient signal function */
    res = ancestor_signal(img, sign, param);
    if(res != LV_RES_OK) return res;

    lv_img_ext_t * ext = lv_obj_get_ext_attr(img);

    if(sign == LV_SIGNAL_GET_STYLE) {
        lv_get_style_info_t * info = param;
        info->result = lv_img_get_style(img, info->part);
    }
    else if(sign == LV_SIGNAL_GET_TYPE) {
        return _lv_obj_handle_get_type_signal(param, LV_OBJX_NAME);
    }
    else  if(sign == LV_SIGNAL_CLEANUP) {
        if(ext->src_type == LV_IMG_SRC_FILE || ext->src_type == LV_IMG_SRC_SYMBOL) {
            lv_mem_free(ext->src);
            ext->src      = NULL;
            ext->src_type = LV_IMG_SRC_UNKNOWN;
        }
    }
    else if(sign == LV_SIGNAL_STYLE_CHG) {
        /*Refresh the file name to refresh the symbol text size*/
        if(ext->src_type == LV_IMG_SRC_SYMBOL) {
            lv_img_set_src(img, ext->src);
        }
    }
    else if(sign == LV_SIGNAL_REFR_EXT_DRAW_PAD) {

        lv_coord_t * s = param;
        lv_coord_t transf_zoom = lv_obj_get_style_transform_zoom(img, LV_IMG_PART_MAIN);
        transf_zoom = (transf_zoom * ext->zoom) >> 8;

        lv_coord_t transf_angle = lv_obj_get_style_transform_angle(img, LV_IMG_PART_MAIN);
        transf_angle += ext->angle;

        /*If the image has angle provide enough room for the rotated corners */
        if(transf_angle || transf_zoom != LV_IMG_ZOOM_NONE) {
            lv_area_t a;
            lv_coord_t w = lv_obj_get_width(img);
            lv_coord_t h = lv_obj_get_height(img);
            _lv_img_buf_get_transformed_area(&a, w, h, transf_angle, transf_zoom, &ext->pivot);
            lv_coord_t pad_ori = *s;
            *s = LV_MATH_MAX(*s, pad_ori - a.x1);
            *s = LV_MATH_MAX(*s, pad_ori - a.y1);
            *s = LV_MATH_MAX(*s, pad_ori + a.x2 - w);
            *s = LV_MATH_MAX(*s, pad_ori + a.y2 - h);
        }

        /*Handle the padding of the background*/
        lv_coord_t left = lv_obj_get_style_pad_left(img, LV_IMG_PART_MAIN);
        lv_coord_t right = lv_obj_get_style_pad_right(img, LV_IMG_PART_MAIN);
        lv_coord_t top = lv_obj_get_style_pad_top(img, LV_IMG_PART_MAIN);
        lv_coord_t bottom = lv_obj_get_style_pad_bottom(img, LV_IMG_PART_MAIN);

        *s = LV_MATH_MAX(*s, left);
        *s = LV_MATH_MAX(*s, right);
        *s = LV_MATH_MAX(*s, top);
        *s = LV_MATH_MAX(*s, bottom);
    }
    else if(sign == LV_SIGNAL_HIT_TEST) {
        lv_hit_test_info_t * info = param;
        lv_coord_t zoom = lv_obj_get_style_transform_zoom(img, LV_IMG_PART_MAIN);
        zoom = (zoom * ext->zoom) >> 8;

        lv_coord_t angle = lv_obj_get_style_transform_angle(img, LV_IMG_PART_MAIN);
        angle += ext->angle;

        /* If the object is exactly image sized (not cropped, not mosaic) and transformed
         * perform hit test on it's transformed area */
        if(ext->w == lv_obj_get_width(img) && ext->h == lv_obj_get_height(img) &&
           (zoom != LV_IMG_ZOOM_NONE || angle != 0 || ext->pivot.x != ext->w / 2 || ext->pivot.y != ext->h / 2)) {

            lv_coord_t w = lv_obj_get_width(img);
            lv_coord_t h = lv_obj_get_height(img);
            lv_area_t coords;
            _lv_img_buf_get_transformed_area(&coords, w, h, angle, zoom, &ext->pivot);
            coords.x1 += img->coords.x1;
            coords.y1 += img->coords.y1;
            coords.x2 += img->coords.x1;
            coords.y2 += img->coords.y1;

            info->result = _lv_area_is_point_on(&coords, info->point, 0);
        }
        else
            info->result = _lv_obj_is_click_point_on(img, info->point);
    }
    else if(sign == LV_SIGNAL_GET_SELF_SIZE) {
        lv_point_t * p = param;
        p->x = ext->w;
        p->y = ext->h;
    }

    return res;
}


static lv_style_list_t * lv_img_get_style(lv_obj_t * img, uint8_t type)
{
    lv_style_list_t * style_dsc_p;
    switch(type) {
        case LV_IMG_PART_MAIN:
            style_dsc_p = &img->style_list;
            break;
        default:
            style_dsc_p = NULL;
    }

    return style_dsc_p;
}

#endif
