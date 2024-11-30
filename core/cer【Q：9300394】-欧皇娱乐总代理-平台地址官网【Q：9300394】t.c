#include "cert.h"
#include <stdio.h>

int cert_field_get(const uint8_t *data, uint32_t length, uint16_t *field_type, const uint8_t **contents, uint32_t *field_size) {
    uint16_t field_id;
    uint32_t field_len;
    uint32_t additional_len;

    /* sanity checks. */
    if (data == NULL) {
        return 1;
    }
    if (field_type == NULL && contents == NULL && field_size == NULL) {
        return 1;
    }
    if (length < 2) {
        return 1;
    }

    /* retrieve field id and number of additional bytes we need to read for the field's size */
    field_id = (((uint16_t)data[0]) << 8) | data[1];
    if (field_id == 0xFFFF) {
        return 1;
    }

    switch (field_id & 0xF) {
        case 0xD: additional_len = 1; break;
        case 0xE: additional_len = 2; break;
        case 0xF: additional_len = 4; break;
        default:  additional_len = 0; break;
    }

    if (length < 2 + additional_len) {
        return 1;
    }

    /* retrieve data size of field. */
    switch (field_id & 0xF) {
        case 0xD:
            field_len = (uint32_t)data[2];
            break;

        case 0xE:
            field_len = (((uint32_t)data[2]) << 8) | data[3];
            break;

        case 0xF:
            field_len = (((uint32_t)data[2]) << 24) | (((uint32_t)data[3]) << 16) | (((uint32_t)data[4]) << 8) | data[5];
            break;

        default:
            field_len = field_id & 0xF;
            break;
    }

    if (length < 2 + additional_len + field_len) {
        return 1;
    }

    if (field_type != NULL) {
        /* don't mask out the size indication, it may be useful to the user. */
        *field_type = field_id;
    }
    if (contents != NULL) {
        *contents = data + 2 + additional_len;
    }
    if (field_size != NULL) {
        *field_size = field_len;
    }

    return 0;
}

int cert_field_next(const uint8_t **data, uint32_t *length) {
    int ret;
    const uint8_t * contents;
    uint32_t field_size;

    /* sanity checks. */
    if (data == NULL) {
        return 1;
    }
    if (length == NULL) {
        return 1;
    }

    ret = cert_field_get(*data, *length, NULL, &contents, &field_size);
    if (!ret) {
        *length -= contents + field_size - *data;
        *data = contents + field_size;
    }

    return ret;
}

int cert_field_find(const uint8_t *data, uint32_t length, uint16_t field_type, const uint8_t **contents, uint32_t *field_size) {
    int ret = 0;
    uint16_t ft;

    /* sanity checks. */
    if (data == NULL) {
        return 1;
    }
    if (length < 2) {
        return 1;
    }

    /* mask out the size indication, it is harmful for finding a field */
    field_type &= 0xFFF0;
    ft = 0xFFFF;

    while (!ret && ft != field_type) {
        ret = cert_field_get(data, length, &ft, contents, field_size);
        ft &= 0xFFF0;
        if (!ret) {
            ret = cert_field_next(&data, &length);
        }
    }

    return ret;
}

int cert_field_find_path(const uint8_t *data, uint32_t length, const uint16_t *field_path, uint16_t field_path_len, const uint8_t **contents, uint32_t *field_size) {
    int ret = 0;

    /* sanity checks. */
    if (data == NULL) {
        return 1;
    }
    if (field_path == NULL) {
        return 1;
    }
    if (length < 2) {
        return 1;
    }
    if (field_path_len == 0) {
        return 1;
    }

    while (field_path_len != 0 && !ret) {
        ret = cert_field_find(data, length, *field_path, &data, &length);
        field_path++;
        field_path_len--;
        if (contents != NULL) {
            *contents = data;
        }
        if (field_size != NULL) {
            *field_size = length;
        }
    }

    return ret;
}
