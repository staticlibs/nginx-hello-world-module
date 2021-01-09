/**
 * @file   ngx_http_hello_world_module.c
 * @author António P. P. Almeida <appa@perusio.net>
 * @date   Wed Aug 17 12:06:52 2011
 *
 * @brief  A hello world module for Nginx.
 *
 * @section LICENSE
 *
 * Copyright (C) 2011 by Dominic Fallows, António P. P. Almeida <appa@perusio.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
extern "C" {
    #include <ngx_config.h>
    #include <ngx_core.h>
    #include <ngx_channel.h>
    #include <ngx_http.h>
}

#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <dlfcn.h>
#include <unistd.h>

static char *ngx_http_hello_world(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_hello_world_handler(ngx_http_request_t *r);

std::pair<int, int>& static_pipe() {
    int fds[2];
    auto pi = pipe(fds);
    if (0 != pi) {
        std::cerr << "pipe failed" << std::endl;
    }
    ngx_nonblocking(fds[0]);
    ngx_nonblocking(fds[1]);
    static auto res = std::make_pair(fds[0], fds[1]);
    return res;
}

/**
 * This module provided directive: hello world.
 *
 */
static ngx_command_t ngx_http_hello_world_commands[] = {

    { ngx_string("hello_world"), /* directive */
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS, /* location context and takes
                                            no arguments*/
      ngx_http_hello_world, /* configuration setup function */
      0, /* No offset. Only one context is supported. */
      0, /* No offset when storing the module configuration on struct. */
      NULL},

    ngx_null_command /* command termination */
};

/* The module context. */
static ngx_http_module_t ngx_http_hello_world_module_ctx = {
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */

    NULL, /* create main configuration */
    NULL, /* init main configuration */

    NULL, /* create server configuration */
    NULL, /* merge server configuration */

    NULL, /* create location configuration */
    NULL /* merge location configuration */
};

typedef char*(*wilton_embed_init_fun)(const char*, int, const char*, int, const char*, int);
typedef char*(*wiltoncall_fun)(const char*, int, const char*, int, char**, int*);
typedef void*(*wilton_free_fun)(char*);

std::string dlerr_str() {
    auto res = ::dlerror();
    return nullptr != res ? std::string(res) : "";
}

void* load_library(const std::string& path) {
    auto lib = ::dlopen(path.c_str(), RTLD_LAZY);
    if (nullptr == lib) {
        throw std::runtime_error(
                "Error loading shared library on path: [" + path + "]," +
                " error: [" + dlerr_str() + "]");
    }
    return lib;
}

void* find_symbol(void* lib, const std::string& name) {
    auto sym = ::dlsym(lib, name.c_str());
    if (nullptr == sym) {
        throw std::runtime_error(
                "Error loading symbol: [" + name + "], " +
                " error: [" + dlerr_str() + "]");
    }
    return sym;
}

void init_wilton() {
    auto whome = std::string("/home/alex/projects/wilton/build/wilton_dist");
    auto engine = std::string("quickjs");
    auto embed_lib = load_library(whome + "/bin/libwilton_embed.so");
    auto embed_init_fun = reinterpret_cast<wilton_embed_init_fun>(find_symbol(embed_lib, "wilton_embed_init"));
    auto core_lib = load_library(whome + "/bin/libwilton_core.so");
    auto wilton_free = reinterpret_cast<wilton_free_fun>(find_symbol(core_lib, "wilton_free"));
    auto app_dir = std::string("/home/alex/projects/wilton_other/wngx");
    auto err_init = embed_init_fun(whome.data(), static_cast<int>(whome.length()),
            engine.data(), static_cast<int>(engine.length()),
            app_dir.data(), static_cast<int>(app_dir.length()));
    if (nullptr != err_init) {
        std::cerr << std::string(err_init) << std::endl;
        wilton_free(err_init);
    }
}

void pipe_event_handler(ngx_event_t* ev) {
    ngx_connection_t* c = static_cast<ngx_connection_t*>(ev->data);
    ngx_int_t result = ngx_handle_read_event(ev, 0);
    if (result != NGX_OK) {
        std::cerr << "pipe: ngx_handle_read_event error: " << result << std::endl;;
    }

    ngx_http_request_t* r;
    ngx_int_t size = read(c->fd, static_cast<void*>(std::addressof(r)), sizeof(r));
    if (size == -1) {
        std::cerr << "pipe: read error" << std::endl;;
    }
    std::cerr << "read" << std::endl;

    auto hello_msg = std::string("hello6\n");

    ngx_buf_t *b;
    ngx_chain_t out;

    /* Set the Content-Type header. */
    r->headers_out.content_type.len = sizeof("text/plain") - 1;
    r->headers_out.content_type.data = (u_char *) "text/plain";

    /* Allocate a new buffer for sending out the reply. */
    b = static_cast<ngx_buf_t*>(ngx_pcalloc(r->pool, sizeof(ngx_buf_t)));

    /* Insertion in the buffer chain. */
    out.buf = b;
    out.next = NULL; /* just one buffer */
    auto vec = std::vector<char>();
    vec.resize(hello_msg.length());
    memcpy(vec.data(), hello_msg.data(), vec.size());
    
    b->pos = reinterpret_cast<unsigned char*>(vec.data()); /* first position in memory of the data */
    b->last = reinterpret_cast<unsigned char*>(vec.data() + vec.size()); /* last position in memory of the data */
    b->memory = 1; /* content is in read-only memory */
    b->last_buf = 1; /* there will be no more buffers in the request */

    /* Sending the headers for the reply. */
    r->headers_out.status = NGX_HTTP_OK; /* 200 status code */
    /* Get the content length of the body. */
    r->headers_out.content_length_n = hello_msg.length();
    ngx_http_send_header(r); /* Send the headers */

    /* Send the body, and return the status code of the output filter chain. */
    ngx_http_output_filter(r, &out);
    ngx_http_finalize_request(r, NGX_HTTP_OK);
}

void init_pipe(ngx_cycle_t* cycle) {
    auto fd = static_pipe().first;
    ngx_int_t rc = ngx_add_channel_event(cycle, fd, NGX_READ_EVENT, pipe_event_handler);
    if (NGX_OK != rc) {
        std::cerr << "init_pipe_error" << std::endl;
    }
}

/* Module definition. */
ngx_module_t ngx_http_hello_world_module = {
    NGX_MODULE_V1,
    &ngx_http_hello_world_module_ctx, /* module context */
    ngx_http_hello_world_commands, /* module directives */
    NGX_HTTP_MODULE, /* module type */
    NULL, /* init master */
    NULL, /* init module */
    [](ngx_cycle_t* cycle) {
        std::cerr << "init process, pid: " << getpid() << std::endl;
        init_wilton();
        init_pipe(cycle);
        return NGX_CONF_OK;
    }, /* init process */
    NULL, /* init thread */
    NULL, /* exit thread */
    [](ngx_cycle_t* /*cycle*/) {
        std::cerr << "shutdown process, pid: " << getpid() << std::endl;
    }, /* exit process */
    NULL, /* exit master */
    NGX_MODULE_V1_PADDING
};

/**
 * Content handler.
 *
 * @param r
 *   Pointer to the request structure. See http_request.h.
 * @return
 *   The status of the response generation.
 */
static ngx_int_t ngx_http_hello_world_handler(ngx_http_request_t *r)
{
    std::cerr << "ngx_http_hello_world_handler" << std::endl;
/*
    std::cerr << "wilton_nginx hello, pid: " << getpid() << std::endl;
    auto whome = std::string("/home/alex/projects/wilton/build/wilton_dist");
    auto core_lib = load_library(whome + "/bin/libwilton_core.so");
    auto wiltoncall = reinterpret_cast<wiltoncall_fun>(find_symbol(core_lib, "wiltoncall"));
    auto wilton_free = reinterpret_cast<wilton_free_fun>(find_symbol(core_lib, "wilton_free"));

    auto call_runscript = std::string("runscript_quickjs");
    auto call_desc_json = std::string(R"({"module": "wngx/hi"})");

    char* json_out = nullptr;
    int json_out_len = -1;
    auto err = wiltoncall(call_runscript.data(), static_cast<int>(call_runscript.length()),
            call_desc_json.data(), static_cast<int>(call_desc_json.length()),
            std::addressof(json_out), std::addressof(json_out_len));

    if (nullptr != err) {
        std::cerr << std::string(err) << std::endl;
        wilton_free(err);
    }
    
    auto hello_msg = std::string(json_out, json_out_len);
    std::cerr << hello_msg << std::endl;
*/


    auto th = std::thread([r] {
        std::cerr << "spawned_thread" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));
        auto fd = static_pipe().second;
        auto written = write(fd, std::addressof(r), sizeof(r));
        std::cerr << "written" << std::endl;
    });
    th.detach();

    r->main->count++;
    return NGX_DONE;

} /* ngx_http_hello_world_handler */

/**
 * Configuration setup function that installs the content handler.
 *
 * @param cf
 *   Module configuration structure pointer.
 * @param cmd
 *   Module directives structure pointer.
 * @param conf
 *   Module configuration structure pointer.
 * @return string
 *   Status of the configuration setup.
 */
static char *ngx_http_hello_world(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf; /* pointer to core location configuration */

    /* Install the hello world handler. */
    clcf = static_cast<ngx_http_core_loc_conf_t*>(ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module));
    clcf->handler = ngx_http_hello_world_handler;

    return NGX_CONF_OK;
} /* ngx_http_hello_world */
