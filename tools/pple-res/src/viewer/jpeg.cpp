// JPEG decoding for the browser build.
//
// The desktop viewer decodes the shipped .jpg sheets with SDL3_image.  SDL3_image has no
// Emscripten port, and building the whole library for wasm would pull in a backend per format
// for a project that only needs one of them, so the web build links Emscripten's own libjpeg
// port instead (jpegsrc v9f through -sUSE_LIBJPEG=1) and decodes here.  The result is the same
// class of decoder as the desktop path's, expanded to the RGBA32 the sheet loader expects.
//
// This file is compiled into pple-view-web only; the desktop target keeps SDL3_image and never
// sees it (see PPLE_HAVE_SDL_IMAGE in resources.cpp).
#include "resources.hpp"

#include <csetjmp>
#include <cstdlib>

extern "C" {
#include <jpeglib.h>
}

namespace pple::viewer {
namespace {

// libjpeg reports a fatal error by calling error_exit, and its default implementation exits the
// process.  This is the documented way to turn that into a return: longjmp back to the call.
struct JpegError {
    jpeg_error_mgr pub;
    std::jmp_buf escape;
    std::string message;
};

void jpeg_fail(j_common_ptr info) {
    auto* error = reinterpret_cast<JpegError*>(info->err);
    char text[JMSG_LENGTH_MAX] = {0};
    if (info->err->format_message != nullptr) (*info->err->format_message)(info, text);
    error->message = text;
    std::longjmp(error->escape, 1);
}

// The default output_message writes warnings to stderr; the note carries what matters, so the
// console stays readable while the game loads a few hundred sheets.
void jpeg_quiet(j_common_ptr info) {
    (void)info;
}

}  // namespace

bool jpeg_decode_rgba(std::span<const std::uint8_t> data, Decoded& out) {
    jpeg_decompress_struct info{};
    JpegError error{};
    info.err = jpeg_std_error(&error.pub);
    error.pub.error_exit = jpeg_fail;
    error.pub.output_message = jpeg_quiet;

    if (setjmp(error.escape) != 0) {
        jpeg_destroy_decompress(&info);
        out.note = "libjpeg: " + (error.message.empty() ? std::string("decode failed") : error.message);
        return false;
    }

    jpeg_create_decompress(&info);
    // libjpeg does not write through the source buffer, but its signature is not const-qualified.
    jpeg_mem_src(&info, const_cast<unsigned char*>(data.data()),
                 static_cast<unsigned long>(data.size()));
    jpeg_read_header(&info, TRUE);
    info.out_color_space = JCS_RGB;
    jpeg_start_decompress(&info);

    const int width = static_cast<int>(info.output_width);
    const int height = static_cast<int>(info.output_height);
    const int channels = static_cast<int>(info.output_components);
    if (width <= 0 || height <= 0 || channels < 3) {
        jpeg_destroy_decompress(&info);
        out.note = "libjpeg: " + std::to_string(width) + "x" + std::to_string(height) + ", " +
                   std::to_string(channels) + " component(s)";
        return false;
    }

    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 255);
    std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * static_cast<std::size_t>(channels));

    while (info.output_scanline < info.output_height) {
        JSAMPROW rows[1] = {row.data()};
        const JDIMENSION y = info.output_scanline;
        jpeg_read_scanlines(&info, rows, 1);
        std::uint8_t* dst = out.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
        for (int x = 0; x < width; ++x) {
            dst[x * 4 + 0] = row[static_cast<std::size_t>(x) * channels + 0];
            dst[x * 4 + 1] = row[static_cast<std::size_t>(x) * channels + 1];
            dst[x * 4 + 2] = row[static_cast<std::size_t>(x) * channels + 2];
            dst[x * 4 + 3] = 255;
        }
    }

    jpeg_finish_decompress(&info);
    jpeg_destroy_decompress(&info);
    out.note = "decoded by libjpeg (web build)";
    return true;
}

}  // namespace pple::viewer
