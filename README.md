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

**Note:** You can get the root CA for a domain easily from Firefox. Click on the green padlock, click *More information > Security > View certificate > Details*. Select the top entry in the 'Certificate Hierarchy' and click *Export...*. This gives you a PEM file. Add the content of the PEM file to your root CA list ([here's an image](img/root-ca-selection.png)).

## Memory usage

Small requests where the body of the response is cached by the library (like the one found in main-http.cpp), require ~4K of RAM. When the request is finished they require ~1.5K of RAM, depending on the size of the response. This applies both to HTTP and HTTPS. If you need to handle requests that return a large response body, see 'Dealing with large body'.

HTTPS requires additional memory. On FRDM-K64F:

* TLS handshake requires 53K of heap space.
* Keeping TLS socket open requires 43K of heap space.

This means that you cannot use HTTPS on devices with less than 128K of memory, as you also need to reserve memory for the stack and network interface.

### Dealing with large response body

By default the library will store the full request body on the heap. This works well for small responses, but you'll run out of memory when receiving a large response body. To mitigate this you can pass in a callback as the last argument to the request constructor. This callback will be called whenever a chunk of the body is received. You can set the request chunk size in the `HTTP_RECEIVE_BUFFER_SIZE` macro (see `mbed_lib.json` for the definition) although it also depends on the buffer size of the underlying network connection.

```cpp
void body_callback(const char* data, uint32_t data_len) {
    // do something with the data
}

HttpRequest* req = new HttpRequest(network, HTTP_GET, "http://pathtolargefile.com", &body_callback);
req->send(NULL, 0);
```

### Dealing with a large request body

If you cannot load the full request into memory, you can pass a callback into the `send` function. Through this callback you can feed in chunks of the request body. This is very useful if you want to send files from a file system.

```cpp
const void * get_chunk(uint32_t* out_size) {
    // set the value of out_size (via *out_size = 10) to the size of the buffer
    // return the buffer

    // if you don't have any more data, set *out_size to 0
}

HttpRequest* req = new HttpRequest(network, HTTP_POST, "http://my_api.com/upload");
req->send(callback(&get_chunk));
```

## Socket re-use

By default the library opens a new socket per request. This is wasteful, especially when dealing with TLS requests. You can re-use sockets like this:

### HTTP

```cpp
TCPSocket* socket = new TCPSocket();

nsapi_error_t open_result = socket->open(network);
// check open_result

nsapi_error_t connect_result = socket->connect("httpbin.org", 80);
// check connect_result

// Pass in `socket`, instead of `network` as first argument
HttpRequest* req = new HttpRequest(socket, HTTP_GET, "http://httpbin.org/status/418");
```

### HTTPS

```cpp
TLSSocket* socket = new TLSSocket(network, "httpbin.org", 443, SSL_CA_PEM);
socket->set_debug(true);
if (socket->connect() != 0) {
    printf("TLS Connect failed %d\n", socket->error());
    return 1;
}

// Pass in `socket`, instead of `network` as first argument, and omit the `SSL_CA_PEM` argument
HttpsRequest* get_req = new HttpsRequest(socket, HTTP_GET, "https://httpbin.org/status/418");
```

## Tested on

* K64F with Ethernet.
* NUCLEO_F411RE with ESP8266.
* ODIN-W2 with WiFi.
* K64F with Atmel 6LoWPAN shield.

But this should work with any Mbed OS 5 device that implements the `NetworkInterface` API.
