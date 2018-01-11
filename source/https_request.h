/*
 * PackageLicenseDeclared: Apache-2.0
 * Copyright (c) 2017 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _MBED_HTTPS_REQUEST_H_
#define _MBED_HTTPS_REQUEST_H_

#include <string>
#include <vector>
#include <map>
#include "http_parser.h"
#include "http_response.h"
#include "http_request_builder.h"
#include "http_request_parser.h"
#include "http_parsed_url.h"
#include "tls_socket.h"

#ifndef HTTP_RECEIVE_BUFFER_SIZE
#define HTTP_RECEIVE_BUFFER_SIZE 8 * 1024
#endif

/**
 * \brief HttpsRequest implements the logic for interacting with HTTPS servers.
 */
class HttpsRequest {
public:
    /**
     * HttpsRequest Constructor
     * Initializes the TCP socket, sets up event handlers and flags.
     *
     * @param[in] net_iface The network interface
     * @param[in] ssl_ca_pem String containing the trusted CAs
     * @param[in] method HTTP method to use
     * @param[in] url URL to the resource
     * @param[in] body_callback Callback on which to retrieve chunks of the response body.
                                If not set, the complete body will be allocated on the HttpResponse object,
                                which might use lots of memory.
     */
    HttpsRequest(NetworkInterface* net_iface,
                 const char* ssl_ca_pem,
                 http_method method,
                 const char* url,
                 Callback<void(const char *at, size_t length)> body_callback = 0)
    {
        _parsed_url = new ParsedUrl(url);
        _body_callback = body_callback;
        _request_builder = new HttpRequestBuilder(method, _parsed_url);
        _response = NULL;
        _debug = false;

        _tlssocket = new TLSSocket(net_iface, _parsed_url->host(), _parsed_url->port(), ssl_ca_pem);
        _we_created_the_socket = true;
    }

    /**
     * HttpsRequest Constructor
     * Sets up event handlers and flags.
     *
     * @param[in] socket A connected TLSSocket
     * @param[in] method HTTP method to use
     * @param[in] url URL to the resource
     * @param[in] body_callback Callback on which to retrieve chunks of the response body.
                                If not set, the complete body will be allocated on the HttpResponse object,
                                which might use lots of memory.
     */
    HttpsRequest(TLSSocket* socket,
                 http_method method,
                 const char* url,
                 Callback<void(const char *at, size_t length)> body_callback = 0)
    {
        _parsed_url = new ParsedUrl(url);
        _body_callback = body_callback;
        _request_builder = new HttpRequestBuilder(method, _parsed_url);
        _response = NULL;
        _debug = false;

        _tlssocket = socket;
        _we_created_the_socket = false;
    }

    /**
     * HttpsRequest Destructor
     */
    ~HttpsRequest() {
        if (_request_builder) {
            delete _request_builder;
        }

        if (_tlssocket && _we_created_the_socket) {
            delete _tlssocket;
        }

        if (_parsed_url) {
            delete _parsed_url;
        }

        if (_response) {
            delete _response;
        }
    }

    /**
     * Execute the HTTPS request.
     *
     * @param[in] body Pointer to the request body
     * @param[in] body_size Size of the request body
     * @return An HttpResponse pointer on success, or NULL on failure.
     *         See get_error() for the error code.
     */
    HttpResponse* send(const void* body = NULL, nsapi_size_t body_size = 0) {
        nsapi_size_or_error_t ret = open_socket();

        if (ret != NSAPI_ERROR_OK) {
            _error = ret;
            return NULL;
        }

        size_t request_size = 0;
        char* request = _request_builder->build(body, body_size, request_size);

        ret = send_buffer((const unsigned char*)request, request_size);

        free(request);

        if (ret < 0) {
            _error = ret;
            return NULL;
        }

        return create_http_response();
    }

    /**
     * Execute the HTTPS request.
     * This sends the request through chunked-encoding.
     * @param body_cb Callback which generates the next chunk of the request
     * @return An HttpResponse pointer on success, or NULL on failure.
     *         See get_error() for the error code.
     */
    HttpResponse* send(Callback<const void*(size_t*)> body_cb) {

        nsapi_error_t ret;

        if ((ret = open_socket()) != NSAPI_ERROR_OK) {
            _error = ret;
            return NULL;
        }

        set_header("Transfer-Encoding", "chunked");

        size_t request_size = 0;
        char* request = _request_builder->build(NULL, 0, request_size);

        // first... send this request headers without the body
        nsapi_size_or_error_t total_send_count = send_buffer((unsigned char*)request, request_size);

        if (total_send_count < 0) {
            free(request);
            _error = total_send_count;
            return NULL;
        }

        // ok... now it's time to start sending chunks...
        while (1) {
            size_t size;
            const void *buffer = body_cb(&size);

            if (size == 0) break;

            // so... size in HEX, \r\n, data, \r\n again
            char size_buff[10]; // if sending length of more than 8 digits, you have another problem on a microcontroller...
            size_t size_buff_size = sprintf(size_buff, "%X\r\n", size);
            if ((total_send_count = send_buffer((const unsigned char*)size_buff, size_buff_size)) < 0) {
                free(request);
                _error = total_send_count;
                return NULL;
            }

            // now send the normal buffer... and then \r\n at the end
            total_send_count = send_buffer((const unsigned char*)buffer, size);
            if (total_send_count < 0) {
                free(request);
                _error = total_send_count;
                return NULL;
            }

            // and... \r\n
            const char* rn = "\r\n";
            if ((total_send_count = send_buffer((const unsigned char*)rn, 2)) < 0) {
                free(request);
                _error = total_send_count;
                return NULL;
            }
        }

        // finalize...?
        const char* fin = "0\r\n\r\n";
        if ((total_send_count = send_buffer((const unsigned char*)fin, strlen(fin))) < 0) {
            free(request);
            _error = total_send_count;
            return NULL;
        }

        free(request);

        return create_http_response();
    }

    /**
     * Closes the underlying TCP socket
     */
    void close() {
        _tlssocket->get_tcp_socket()->close();
    }

    /**
     * Set a header for the request.
     *
     * The 'Host' and 'Content-Length' headers are set automatically.
     * Setting the same header twice will overwrite the previous entry.
     *
     * @param[in] key Header key
     * @param[in] value Header value
     */
    void set_header(string key, string value) {
        _request_builder->set_header(key, value);
    }

    /**
     * Get the error code.
     *
     * When send() fails, this error is set.
     */
    nsapi_error_t get_error() {
        return _error;
    }

    /**
     * Set the debug flag.
     *
     * If this flag is set, debug information from mbed TLS will be logged to stdout.
     */
    void set_debug(bool debug) {
        _debug = debug;

        _tlssocket->set_debug(debug);
    }


protected:
    /**
     * Helper for pretty-printing mbed TLS error codes
     */
    static void print_mbedtls_error(const char *name, int err) {
        char buf[128];
        mbedtls_strerror(err, buf, sizeof (buf));
        mbedtls_printf("%s() failed: -0x%04x (%d): %s\r\n", name, -err, err, buf);
    }

    void onError(TCPSocket *s, int error) {
        s->close();
        _error = error;
    }

    nsapi_error_t onErrorAndReturn(TCPSocket *s, int error) {
        s->close();
        return error;
    }

private:
    nsapi_error_t open_socket() {
        // not tried to connect before?
        if (_tlssocket->error() != 0) {
            return _tlssocket->error();
        }

        _socket_was_open = _tlssocket->connected();

        if (!_socket_was_open) {
            nsapi_error_t r = _tlssocket->connect();
            if (r != NSAPI_ERROR_OK) {
                return r;
            }
        }

        return NSAPI_ERROR_OK;
    }

    nsapi_size_or_error_t send_buffer(const unsigned char *buffer, size_t buffer_size) {
        nsapi_size_or_error_t ret = mbedtls_ssl_write(_tlssocket->get_ssl_context(), (const unsigned char *) buffer, buffer_size);

        if (ret < 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
                ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                print_mbedtls_error("mbedtls_ssl_write", ret);
                return onErrorAndReturn(_tlssocket->get_tcp_socket(), -1 );
            }
            else {
                return ret;
            }
        }

        return NSAPI_ERROR_OK;
    }

    HttpResponse* create_http_response() {
        nsapi_size_or_error_t ret;

        // Create a response object
        _response = new HttpResponse();
        // And a response parser
        HttpParser parser(_response, HTTP_RESPONSE, _body_callback);

        // Set up a receive buffer (on the heap)
        uint8_t* recv_buffer = (uint8_t*)malloc(HTTP_RECEIVE_BUFFER_SIZE);

        /* Read data out of the socket */
        while ((ret = mbedtls_ssl_read(_tlssocket->get_ssl_context(), (unsigned char *) recv_buffer, HTTP_RECEIVE_BUFFER_SIZE)) > 0) {
            // Don't know if this is actually needed, but OK
            size_t _bpos = static_cast<size_t>(ret);
            recv_buffer[_bpos] = 0;

            size_t nparsed = parser.execute((const char*)recv_buffer, _bpos);
            if (nparsed != _bpos) {
                print_mbedtls_error("parser_error", nparsed);
                // parser error...
                _error = -2101;
                free(recv_buffer);
                return NULL;
            }

            if (_response->is_message_complete()) {
                break;
            }
        }
        if (ret < 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                print_mbedtls_error("mbedtls_ssl_read", ret);
                onError(_tlssocket->get_tcp_socket(), -1 );
            }
            else {
                _error = ret;
            }
            free(recv_buffer);
            return NULL;
        }

        parser.finish();

        if (!_socket_was_open) {
            _tlssocket->get_tcp_socket()->close();
        }

        free(recv_buffer);

        return _response;
    }

protected:
    TLSSocket* _tlssocket;
    bool _we_created_the_socket;

    Callback<void(const char *at, size_t length)> _body_callback;
    ParsedUrl* _parsed_url;
    HttpRequestBuilder* _request_builder;
    HttpResponse* _response;

    bool _socket_was_open;

    nsapi_error_t _error;
    bool _debug;

};

#endif // _MBED_HTTPS_REQUEST_H_
