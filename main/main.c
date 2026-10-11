#define IBase GS_IBase

#include "gs_id.h"
#include "qrsp.h"
#include "srv_ucm_interface.h"
#include "vmsys.h"
#include "vmtimer.h"

extern VMINT vm_create_service(VMINT sid, VMINT iid, void** interface_ptr);

VM_CAMERA_HANDLE handle_ptr;
VMUINT8* layer_buf0 = NULL;
char* tmp_qr_res = NULL;
int font_height = 16;
VMBOOL capture_stage = VM_TRUE;
VMBOOL is_malloc = VM_FALSE;
VMINT sw;
VMINT sh;
VMINT scan_timer_id = -1;
VMBOOL scan_enabled = VM_FALSE;

typedef enum { STATE_SCANNING, STATE_IN_CALL, STATE_ERROR_MESSAGE } APP_STATE;

APP_STATE current_state = STATE_SCANNING;
VMINT32 error_timer_id = -1;
char error_display_text[64] = "";

void handle_sysevt(VMINT message, VMINT param);
void handle_keyevt(VMINT event, VMINT keycode);
void start_cam_preview(void);
void app_set_current_preview_size(VM_CAMERA_HANDLE camera_handle);
void cam_message_callback(vm_cam_notify_data_t* notify_data, void* user_data);
// void cam_message_callback(const vm_cam_notify_data_t* notify_data, void* user_data);
void trigger_error_state(const char* message);
void invertGrayBuffer(unsigned char* buf, unsigned int size);
void uyvyToGrey(unsigned char* dst, unsigned char* src, unsigned int numberPixels);
char* decode_qr(unsigned char* data, size_t data_sz, int width, int height);
unsigned char claim(int x);
static VMBOOL is_phone_number(const char* s);
void scan_enable_timer_callback(VMINT tid);
void normalize_phone(char* dst, const char* src);

//void create_app_txt_filenamex(VMWSTR text, VMSTR extt) {

//    VMINT drv;
//    VMWCHAR fullPath[100] = {0};
//    VMWCHAR appName[100] = {0};
//    VMWCHAR wfile_extension[8] = {0};
//    VMCHAR fAutoFileName[100] = {0};
//    VMWCHAR wAutoFileName[100] = {0};
//    VMWCHAR wProduct[100] = {0};

//    vm_ascii_to_ucs2(wfile_extension, 8, extt);

//    if ((drv = vm_get_removable_driver()) < 0) {
//        drv = vm_get_system_driver();
//    }

//    sprintf(fAutoFileName, "%c:\\", drv);
//    vm_ascii_to_ucs2(wProduct, (strlen(fAutoFileName) + 1) * 2, fAutoFileName);
//    vm_get_exec_filename(fullPath);
//    vm_get_filename(fullPath, appName);
//    vm_wstrncpy(wAutoFileName, appName, vm_wstrlen(appName) - 3);
//    vm_wstrcat(wAutoFileName, wfile_extension);
//    vm_wstrcat(wProduct, wAutoFileName);
//    vm_wstrcpy(text, wProduct);
//}

//void log_debug(const char* fmt, ...) {

//    VMWCHAR file_pathw[256];
//    create_app_txt_filenamex(file_pathw, (VMSTR) "txt");

//    VMFILE f = vm_file_open(file_pathw, MODE_APPEND, FALSE);
//    if (f < 0) f = vm_file_open(file_pathw, MODE_CREATE_ALWAYS_WRITE, FALSE);

//    if (f < 0) return;

//    char buf[256];

//    va_list ap;
//    va_start(ap, fmt);
//    vsnprintf(buf, sizeof(buf), fmt, ap);
//    va_end(ap);

//    VMUINT nwrite;
//    vm_file_write(f, buf, strlen(buf), &nwrite);

//    vm_file_close(f);
//}

void vm_main(void) {

    layer_hdl[0] = -1;
    handle_ptr = -1;
    vm_reg_sysevt_callback(handle_sysevt);
    vm_reg_keyboard_callback(handle_keyevt);
    vm_font_set_font_size(VM_SMALL_FONT);
    sw = vm_graphic_get_screen_width();
    sh = vm_graphic_get_screen_height();
    //log_debug("FRAME_CONST=%d\r\n", VM_CAM_PREVIEW_FRAME_RECEIVED);
}

void reset_to_preview(void) {

    scan_enabled = VM_FALSE;

    if (scan_timer_id != -1) {
        vm_delete_timer(scan_timer_id);
        scan_timer_id = -1;
    }

    scan_timer_id = vm_create_timer(2000, scan_enable_timer_callback);  // 1000

    current_state = STATE_SCANNING;
    capture_stage = VM_TRUE;
    is_malloc = VM_FALSE;
    if (tmp_qr_res) {
        vm_free(tmp_qr_res);
        tmp_qr_res = NULL;
    }

    if (handle_ptr == -1) {
        start_cam_preview();
    } else {
        if (vm_camera_preview_start(handle_ptr) != VM_CAM_SUCCESS) {
            // vm_camera_preview_stop(handle_ptr);
            vm_release_camera_instance(handle_ptr);
            handle_ptr = -1;
            // trigger_error_state("Preview Fail");
        }
    }
}

void error_timer_callback(int timer_id) {

    vm_delete_timer(timer_id);
    error_timer_id = -1;
    reset_to_preview();
}

void trigger_error_state(const char* message) {

    current_state = STATE_ERROR_MESSAGE;
    strcpy(error_display_text, message);

    if (layer_hdl[0] != -1 && layer_buf0) {
        VMWCHAR w_msg[64];
        memset(w_msg, 0, sizeof(w_msg));
        vm_graphic_fill_rect(layer_buf0, 0, 0, sw, sh, VM_COLOR_WHITE, VM_COLOR_WHITE);
        vm_ascii_to_ucs2(w_msg, 64 * 2, error_display_text);
        vm_graphic_textout(layer_buf0, 20, 140, w_msg, vm_wstrlen(w_msg), VM_COLOR_RED);

        // VMINT hw_hdls = layer_hdl[0];
        vm_graphic_flush_layer(layer_hdl, 1);
    }

    error_timer_id = vm_create_timer(2000, error_timer_callback);
}

static S32 dial_callback(gs_srv_event_struct* param) { return 0; }

void make_background_call(const char* number_str) {

    IUcm* ucm = NULL;

    VMINT ret = vm_create_service(SID_UCMSERVICE, IID_IUCM, (void**)&ucm);

    if (ret < 0 || !ucm) {
        // trigger_error_state("UCM Init Fail");
        return;
    }

    gs_srv_ucm_dial_act_req_struct req;

    memset(&req, 0, sizeof(req));

    req.call_type = GS_SRV_UCM_VOICE_CALL_TYPE;
    req.module_id = GS_SRV_UCM_MODULE_ORIGIN_COMMON;
    req.is_ip_dial = 0;

    vm_ascii_to_ucs2((VMWSTR)req.num_uri, sizeof(req.num_uri), (VMCHAR*)number_str);

    if (IUcm_query_act_permit(ucm, GS_SRV_UCM_DIAL_ACT, &req) == GS_SRV_UCM_RESULT_OK) {

        current_state = STATE_IN_CALL;
        IUcm_act_request(ucm, GS_SRV_UCM_DIAL_ACT, &req, NULL, dial_callback);

    } else {
        // trigger_error_state("Permit Denied");
    }

    IUcm_release(ucm);

    if (tmp_qr_res) {
        vm_free(tmp_qr_res);
        tmp_qr_res = NULL;
    }
}

void handle_sysevt(VMINT message, VMINT param) {

    switch (message) {
            //        case VM_MSG_CREATE:
            //            if (layer_hdl[0] == -1) {
            //                layer_hdl[0] = vm_graphic_create_layer(0, 0, sw,
            //                sh, -1);
            //            }
            //            vm_graphic_set_clip(0, 0, sw, sh);
            //            layer_buf0 =
            //            vm_graphic_get_layer_buffer(layer_hdl[0]);
            //            vm_switch_power_saving_mode(turn_off_mode);
            //            reset_to_preview();
            //            break;

            //        case VM_MSG_ACTIVE:
            //            vm_switch_power_saving_mode(turn_off_mode);
            //            if (layer_hdl[0] == -1) {
            //                layer_hdl[0] = vm_graphic_create_layer(0, 0, sw,
            //                sh, -1); layer_buf0 =
            //                vm_graphic_get_layer_buffer(layer_hdl[0]);
            //            }
            //            vm_graphic_set_clip(0, 0, sw, sh);

            ////            if (current_state == STATE_IN_CALL) {
            ////                reset_to_preview();
            ////            } else if (current_state == STATE_SCANNING) {
            ////                reset_to_preview();  // is is nessesary, extra
            ///camera restarts while switching screens. ? /            }

            ////if (current_state == STATE_IN_CALL)
            ////{
            ////    reset_to_preview();
            ////}

            //    if (handle_ptr == -1)
            //    {
            //        start_cam_preview();
            //    }

        case VM_MSG_CREATE:
        case VM_MSG_ACTIVE:

            if (layer_hdl[0] == -1) {
                layer_hdl[0] = vm_graphic_create_layer(0, 0, sw, sh, -1);
            }

            // log_debug("L=%d\r\n", layer_hdl[0]);

            vm_graphic_set_clip(0, 0, sw, sh);

            layer_buf0 = vm_graphic_get_layer_buffer(layer_hdl[0]);

            vm_switch_power_saving_mode(turn_off_mode);

            //    start_cam_preview();

            if (handle_ptr == -1) {
                start_cam_preview();
            }

            break;

        case VM_MSG_PAINT:
            vm_switch_power_saving_mode(turn_off_mode);
            if (current_state == STATE_ERROR_MESSAGE && layer_buf0) {
                VMWCHAR w_msg[64];
                memset(w_msg, 0, sizeof(w_msg));
                vm_graphic_fill_rect(layer_buf0, 0, 0, sw, sh, VM_COLOR_WHITE, VM_COLOR_WHITE);
                vm_ascii_to_ucs2(w_msg, 64 * 2, error_display_text);
                vm_graphic_textout(layer_buf0, 20, 140, w_msg, vm_wstrlen(w_msg), VM_COLOR_RED);

                VMINT hw_hdls = layer_hdl[0];
                vm_graphic_flush_layer(layer_hdl, 1);
            }
            break;

        case VM_MSG_INACTIVE:

            if (current_state != STATE_IN_CALL) {
                if (handle_ptr != -1) {
                    vm_camera_preview_stop(handle_ptr);
                    vm_release_camera_instance(handle_ptr);
                    handle_ptr = -1;
                }

                // vm_release_camera_instance(handle_ptr);
                if (layer_hdl[0] != -1) {
                    vm_graphic_delete_layer(layer_hdl[0]);
                    layer_hdl[0] = -1;
                    layer_buf0 = NULL;
                }
                vm_graphic_reset_clip();
                vm_switch_power_saving_mode(turn_on_mode);
            }
            break;

        case VM_MSG_QUIT:
            if (error_timer_id != -1) vm_delete_timer(error_timer_id);
            if (layer_hdl[0] != -1) {
                vm_graphic_delete_layer(layer_hdl[0]);
                layer_hdl[0] = -1;
                layer_buf0 = NULL;
            }
            if (handle_ptr != -1) {
                vm_camera_preview_stop(handle_ptr);
                vm_release_camera_instance(handle_ptr);
                handle_ptr = -1;
            }

            if (scan_timer_id != -1) {
                vm_delete_timer(scan_timer_id);
                scan_timer_id = -1;
            }

            break;

        default:
            break;
    }
}

void handle_keyevt(VMINT event, VMINT keycode) {

    if (event == VM_KEY_EVENT_UP && keycode == VM_KEY_RIGHT_SOFTKEY) {
        if (error_timer_id != -1) vm_delete_timer(error_timer_id);
        if (layer_hdl[0] != -1) {
            vm_graphic_delete_layer(layer_hdl[0]);
            layer_hdl[0] = -1;
            layer_buf0 = NULL;
        }

        vm_exit_app();
    }
}

void start_cam_preview(void) {

    // VMINT ret;

    // ret = vm_create_camera_instance(VM_CAMERA_MAIN_ID, &handle_ptr);
    // ret =
    // vm_create_camera_instance((VM_CAMERA_ID)vm_camera_get_main_camera_id(),
    // &handle_ptr); log_debug("C=%d\r\n", ret);
    if (vm_create_camera_instance(VM_CAMERA_MAIN_ID, &handle_ptr) != VM_CAM_SUCCESS) {
        //    if (ret != VM_CAM_SUCCESS) {
        //        trigger_error_state("Create Cam Fail");
        return;
    }

    // ret = vm_camera_register_notify(handle_ptr,
    // (VM_CAMERA_STATUS_NOTIFY)cam_message_callback, NULL); ret =
    // vm_camera_register_notify(handle_ptr, cam_message_callback, NULL);

    // log_debug("N=%d\r\n", ret);
    if (vm_camera_register_notify(handle_ptr, (VM_CAMERA_STATUS_NOTIFY)cam_message_callback, NULL) != VM_CAM_SUCCESS) {
        vm_release_camera_instance(handle_ptr);
        handle_ptr = -1;
        //        trigger_error_state("Other Fail");
        return;
    }

    app_set_current_preview_size(handle_ptr);

    // ret = vm_camera_preview_start(handle_ptr);
    // log_debug("P=%d\r\n", ret);
    //     if (vm_camera_preview_start(handle_ptr) != VM_CAM_SUCCESS) {
    //     if (ret != VM_CAM_SUCCESS) {
    //         vm_release_camera_instance(handle_ptr);
    //         handle_ptr = -1;
    //         trigger_error_state("Preview Start Fail");
    //         return;
    //     }
    // }

    if (vm_camera_preview_start(handle_ptr) != VM_CAM_SUCCESS) {
        vm_release_camera_instance(handle_ptr);
        handle_ptr = -1;
        return;
    }

    scan_enabled = VM_FALSE;

    if (scan_timer_id != -1) {
        vm_delete_timer(scan_timer_id);
    }

    scan_timer_id = vm_create_timer(2000, scan_enable_timer_callback);  // 1000
}

void cam_message_callback(vm_cam_notify_data_t* notify_data, void* user_data) {

    // void cam_message_callback(const vm_cam_notify_data_t* notify_data, void* user_data) {

    //    log_debug("MSG=%d\r\n", notify_data->cam_message);
    vm_cam_frame_data_t my_frame_data;
    int i;

    handle_ptr = notify_data->handle;

    switch (notify_data->cam_message) {
        case VM_CAM_PREVIEW_FRAME_RECEIVED:
            //            log_debug("F\r\n");
            //            if (current_state == STATE_SCANNING &&
            //                vm_camera_get_frame(handle_ptr, &my_frame_data) ==
            //                VM_CAM_SUCCESS) {

            ////              if (vm_camera_get_frame(handle_ptr,
            ///&my_frame_data) == VM_CAM_SUCCESS) {

            if (vm_camera_get_frame(handle_ptr, &my_frame_data) !=
                VM_CAM_SUCCESS)
                return;

            // VMINT ret = vm_camera_get_frame(handle_ptr, &my_frame_data);

            // log_debug("W=%d H=%d\r\n", my_frame_data.col_pixel,
            // my_frame_data.row_pixel);

            // log_debug("GF=%d\r\n", ret);

            // if (ret != VM_CAM_SUCCESS)
            //     return;

            VMUINT row_pixel = my_frame_data.row_pixel;
            VMUINT col_pixel = my_frame_data.col_pixel;
            VMUINT grey_app_frame_data_size = row_pixel * col_pixel;
            //                unsigned char* grey_app_frame_data = vm_malloc(grey_app_frame_data_size);
            //                if (!grey_app_frame_data) {
            //                    return;
            //                }

            unsigned char* uyvy_data = my_frame_data.pixtel_data;
            //                uyvyToGrey(grey_app_frame_data, uyvy_data, grey_app_frame_data_size);
            //                tmp_qr_res = decode_qr(grey_app_frame_data, grey_app_frame_data_size, col_pixel, row_pixel);

            ////tmp_qr_res = NULL;

            // log_debug("QR=%p\r\n", tmp_qr_res);

            /// if (tmp_qr_res)
            //{
            //    log_debug("QRTXT=%s\r\n", tmp_qr_res);
            //}

            //                if (!tmp_qr_res) {
            //                    invertGrayBuffer(grey_app_frame_data, grey_app_frame_data_size);
            //                    tmp_qr_res = decode_qr(grey_app_frame_data, grey_app_frame_data_size, col_pixel, row_pixel);
            ////tmp_qr_res = NULL;
            //                }

            tmp_qr_res = NULL;

            if (scan_enabled) {
                unsigned char* grey_app_frame_data = vm_malloc(grey_app_frame_data_size);

                if (!grey_app_frame_data) return;

                uyvyToGrey(grey_app_frame_data, uyvy_data, grey_app_frame_data_size);

                tmp_qr_res = decode_qr(grey_app_frame_data, grey_app_frame_data_size, col_pixel, row_pixel);

                if (!tmp_qr_res) {
                    invertGrayBuffer(grey_app_frame_data, grey_app_frame_data_size);

                    tmp_qr_res = decode_qr(grey_app_frame_data, grey_app_frame_data_size, col_pixel, row_pixel);
                }

                vm_free(grey_app_frame_data);
            }

            //                vm_free(grey_app_frame_data);
            if (tmp_qr_res) {
                is_malloc = VM_TRUE;
                vm_camera_preview_stop(handle_ptr);
                return;
            }
            VMWCHAR* layer_buf_s = (VMWCHAR*)layer_buf0;
            if (layer_buf_s) {
                int x, y;

                for (y = 0; y < sw; y++) {
                    for (x = 0; x < sw; x += 2) {
                        int u0 = *uyvy_data++ - 128;
                        int y0 = *uyvy_data++ - 16;
                        int v0 = *uyvy_data++ - 128;
                        int y1 = *uyvy_data++ - 16;

                        int tmp = 298 * y0 + 128;
                        int vv = 409 * v0;
                        int uv = -100 * u0 - 208 * v0;
                        int uu = 516 * u0;

                        layer_buf_s[y * sw + x] = VM_COLOR_888_TO_565(
                            claim((tmp + vv) >> 8), claim((tmp + uv) >> 8),
                            claim((tmp + uu) >> 8));

                        tmp = 298 * y1 + 128;

                        layer_buf_s[y * sw + x + 1] = VM_COLOR_888_TO_565(
                            claim((tmp + vv) >> 8), claim((tmp + uv) >> 8),
                            claim((tmp + uu) >> 8));
                    }
                }

                vm_graphic_fill_rect(layer_buf0, 0, sw, sw, sh / 4, VM_COLOR_WHITE, VM_COLOR_WHITE);

                vm_graphic_flush_layer(layer_hdl, 1);
            }

            // log_debug("W=%d H=%d\r\n", my_frame_data.col_pixel, my_frame_data.row_pixel);

            //            }
            break;
        case VM_CAM_PREVIEW_STOP_DONE:
            if (tmp_qr_res) {

                VMWCHAR wtxt[128];

                vm_ascii_to_ucs2(wtxt, sizeof(wtxt), tmp_qr_res);

                vm_graphic_fill_rect(layer_buf0, 0, sw, sw, sh / 4, VM_COLOR_WHITE, VM_COLOR_WHITE);

                vm_graphic_textout(layer_buf0, 2, 250, wtxt, vm_wstrlen(wtxt), VM_COLOR_BLACK);

                vm_graphic_flush_layer(layer_hdl, 1);

                char upper_buf[128];

                strncpy(upper_buf, tmp_qr_res, sizeof(upper_buf) - 1);
                upper_buf[sizeof(upper_buf) - 1] = '\0';

                vm_upper_case(upper_buf, upper_buf);

                if (strncmp(upper_buf, "TEL:", 4) == 0) {
                    char* phone_num = tmp_qr_res + 4;

                    if (strchr(phone_num, ';') || strchr(phone_num, ',')) {
                        /* Multiple phone numbers detected */
                        // trigger_error_state("Multiple numbers");

                        reset_to_preview();
                        break;
                    }

                    char normalized[128];

                    normalize_phone(normalized, phone_num);

                    if (is_phone_number(normalized)) {
                        char temp_text[128];

                        snprintf(temp_text, sizeof(temp_text), "Start phone call: %s", normalized);

                        vm_ascii_to_ucs2(wtxt, sizeof(wtxt), temp_text);

                        vm_graphic_fill_rect(layer_buf0, 0, sw, sw, sh / 4, VM_COLOR_WHITE, VM_COLOR_WHITE);

                        vm_graphic_textout(layer_buf0, 2, 250, wtxt, vm_wstrlen(wtxt), VM_COLOR_BLACK);

                        vm_graphic_flush_layer(layer_hdl, 1);

                         make_background_call(normalized);

                    } else {
                        //                    trigger_error_state("Wrong number format");
                    }

                } else {
                    //                    trigger_error_state("No number");
                }
            } else {
                reset_to_preview();
            }
            break;
        default:
            //    log_debug("MSG=%d\r\n", notify_data->cam_message);
            break;
    }
}

void app_set_current_preview_size(VM_CAMERA_HANDLE camera_handle) {

    const vm_cam_size_t* ptr = NULL;
    VMUINT size = 0, i = 0;
    if (vm_camera_get_support_preview_size(camera_handle, &ptr, &size) == VM_CAM_SUCCESS) {
        vm_cam_size_t my_cam_size;
        for (i = 0; i < size; i++) {
            my_cam_size.width = (ptr + i)->width;
            my_cam_size.height = (ptr + i)->height;
        }
        my_cam_size.width = sw;   // 240; 240; sw
        my_cam_size.height = sh;  // 320; 240; sh

        vm_camera_set_preview_size(camera_handle, &my_cam_size);

        // VMINT ret = vm_camera_set_preview_size(camera_handle, &my_cam_size);
        // log_debug("SET=%d %dx%d\r\n", ret, my_cam_size.width,
        // my_cam_size.height);
    }
}

void invertGrayBuffer(unsigned char* buf, unsigned int size) {

    unsigned int i;
    for (i = 0; i < size; ++i) {
        buf[i] = 255 - buf[i];
    }
}

void uyvyToGrey(unsigned char* dst, unsigned char* src, unsigned int numberPixels) {

    while (numberPixels > 0) {
        src++;
        uint8_t y1 = *(src++);
        src++;
        uint8_t y2 = *(src++);
        *(dst++) = y1;
        *(dst++) = y2;
        numberPixels -= 2;
    }
}

char* decode_qr(unsigned char* data, size_t data_sz, int width, int height) {

    char* decoded_str = NULL;
    zbar_image_scanner_t* scanner = zbar_image_scanner_create();
    zbar_image_t* image = zbar_image_create();
    zbar_image_scanner_set_config(scanner, 0, ZBAR_CFG_ENABLE, 1);
    zbar_image_set_format(image, *(int*)"GREY");
    zbar_image_set_size(image, width, height);
    zbar_image_set_data(image, data, data_sz, NULL);
    if (zbar_scan_image(scanner, image) > 0) {
        const zbar_symbol_t* symbol = zbar_image_first_symbol(image);
        if (symbol) {
            const char* symbol_data = zbar_symbol_get_data(symbol);
            decoded_str = vm_malloc(strlen(symbol_data) + 1);
            if (decoded_str) {
                strcpy(decoded_str, symbol_data);
            }
        }
    }
    zbar_image_destroy(image);
    zbar_image_scanner_destroy(scanner);
    return decoded_str;
}

unsigned char claim(int x) { return x > 255 ? 255 : (x < 0 ? 0 : x); }

static VMBOOL is_phone_number(const char* s) {

    int digits = 0;

    if (!s || !*s) return VM_FALSE;

    if (*s == '+') s++;

    while (*s) {
        if (*s < '0' || *s > '9') return VM_FALSE;

        digits++;
        s++;
    }

    return (digits >= 5);
}

void scan_enable_timer_callback(VMINT tid) {

    vm_delete_timer(tid);
    scan_timer_id = -1;

    scan_enabled = VM_TRUE;
}

void normalize_phone(char* dst, const char* src) {

    char* dst_start = dst;

    while (*src == ' ' || *src == '\t') {
        src++;
    }

    while (*src) {
        if (*src >= '0' && *src <= '9') {
            *dst++ = *src;
        } else if (*src == '+' && dst == dst_start) {
            *dst++ = *src;
        }

        src++;
    }

    *dst = '\0';
}
