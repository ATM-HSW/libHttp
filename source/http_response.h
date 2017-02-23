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

#ifndef _MBED_HTTP_HTTP_RESPONSE
#define _MBED_HTTP_HTTP_RESPONSE
#include <string>
#include <vector>
#include "http_parser.h"

using namespace std;

class HttpResponse {
public:
    HttpResponse() {
        status_code = 0;
        concat_header_field = false;
        concat_header_value = false;
        expected_content_length = 0;
        body_length = 0;
    }

    void set_status(int a_status_code, string a_status_message) {
        status_code = a_status_code;
        status_message = a_status_message;
    }

    int get_status_code() {
        return status_code;
    }

    string get_status_message() {
        return status_message;
    }

    void set_header_field(string field) {
        concat_header_value = false;

        // headers can be chunked
        if (concat_header_field) {
            header_fields[header_fields.size() - 1] = header_fields[header_fields.size() - 1] + field;
        }
        else {
            header_fields.push_back(field);
        }

        concat_header_field = true;
    }

    void set_header_value(string value) {
        concat_header_field = false;

        // headers can be chunked
        if (concat_header_value) {
            header_values[header_values.size() - 1] = header_values[header_values.size() - 1] + value;
        }
        else {
            header_values.push_back(value);
        }

        concat_header_value = true;
    }

    void set_headers_complete() {
        // @todo: chunked encoding
        for (size_t ix = 0; ix < header_fields.size(); ix++) {
            if (strcicmp(header_fields[ix].c_str(), "content-length") == 0) {
                expected_content_length = (size_t)atoi(header_values[ix].c_str());
                break;
            }
        }
    }

    size_t get_headers_length() {
        return header_fields.size();
    }

    vector<string> get_headers_fields() {
        return header_fields;
    }

    vector<string> get_headers_values() {
        return header_values;
    }

    void set_body(string v) {
        body = body + v;
    }

    string get_body() {
        return body;
    }

    void increase_body_length(size_t length) {
        body_length += length;
    }

    bool is_body_complete() {
        return body_length == expected_content_length;
    }

private:
    // from http://stackoverflow.com/questions/5820810/case-insensitive-string-comp-in-c
    int strcicmp(char const *a, char const *b) {
        for (;; a++, b++) {
            int d = tolower(*a) - tolower(*b);
            if (d != 0 || !*a) {
                return d;
            }
        }
    }

    int status_code;
    string status_message;

    vector<string> header_fields;
    vector<string> header_values;

    bool concat_header_field;
    bool concat_header_value;

    size_t expected_content_length;

    string body;
    size_t body_length;
};
#endif
