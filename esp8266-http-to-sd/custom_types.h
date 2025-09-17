#ifndef __CUSTOM_TYPES_H
#define __CUSTOM_TYPES_H

typedef void (*RouteHandlerFunction)(WiFiClient client);

typedef struct {
    const char* path;             // A string a ser procurada na requisição, ex: "GET / HTTP"
    RouteHandlerFunction handler; // O ponteiro para a função que vai cuidar da requisição
} RouteItem;

#endif /* __CUSTOM_TYPES_H */