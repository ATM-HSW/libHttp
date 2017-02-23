# HTTP and HTTPS library for mbed OS 5

This library is used to make HTTP and HTTPS calls from mbed OS 5 applications.

## HTTP Request API

```cpp
NetworkInterface* network = /* obtain a NetworkInterface object */

const char body[] = "{\"hello\":\"world\"}";

HttpRequest* request = new HttpRequest(network, HTTP_POST, "http://httpbin.org/post");
request->set_header("Content-Type", "application/json");
HttpResponse* response = request->send(body, strlen(body));
// if response is NULL, check response->get_error()

printf("status is %d - %s\n", response->get_status_code(), response->get_status_message());
printf("body is:\n%s\n", response->get_body_as_string().c_str());

delete request; // also clears out the response
```

## HTTPS Request API

```cpp
// pass in the root certificates that you trust, there is no central CA registry in mbed OS
const char SSL_CA_PEM[] = "-----BEGIN CERTIFICATE-----\n"
    /* rest of the CA root certificates */;

NetworkInterface* network = /* obtain a NetworkInterface object */

const char body[] = "{\"hello\":\"world\"}";

HttpsRequest* request = new HttpsRequest(network, SSL_CA_PEM, HTTP_GET "https://httpbin.org/status/418");
HttpResponse* response = request->send();
// if response is NULL, check response->get_error()

printf("status is %d - %s\n", response->get_status_code(), response->get_status_message());
printf("body is:\n%s\n", response->get_body().c_str());

delete request;
```

## Dealing with large body

By default the library will store the full request body on the heap. This works well for small responses, but you'll run out of memory when receiving a large response body. To mitigate this you can pass in a callback as the last argument to the request constructor. This callback will be called whenever a chunk of the body is received. You can set the request chunk size in the `HTTP_RECEIVE_BUFFER_SIZE` macro (see `mbed_lib.json` for the definition) although it also depends on the buffer size of the underlying network connection.

```cpp
void body_callback(const char* data, size_t data_len) {
    // do something with the data
}

HttpRequest* req = new HttpRequest(network, HTTP_GET, "http://pathtolargefile.com");
req->send(NULL, 0, body_callback);
```

## Tested on

* K64F with Ethernet.
* NUCLEO_F411RE with ESP8266.
