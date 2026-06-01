/**
 * @file rime_config.c
 * @brief Configuration loading and cleanup for the Rime engine
 */

#include "rime_internal.h"

void typio_rime_free_config(TypioRimeConfig *config) {
    if (!config) {
        return;
    }

    free(config->schema);
    free(config->shared_data_dir);
    free(config->user_data_dir);
    memset(config, 0, sizeof(*config));
}

TypioResult typio_rime_load_config(TypioEngine *engine,
                                   TypioInstance *instance,
                                   TypioRimeConfig *config) {
    const char *engine_data_dir;
    TypioConfig *engine_config = nullptr;

    if (!engine || !instance || !config) {
        return TYPIO_ERROR_INVALID_ARGUMENT;
    }

    memset(config, 0, sizeof(*config));
    config->schema = typio_strdup(TYPIO_RIME_DEFAULT_SCHEMA);
    config->shared_data_dir = typio_strdup(TYPIO_RIME_SHARED_DATA_DIR);

    engine_data_dir = typio_instance_get_engine_data_dir(instance, "rime");
    if (engine_data_dir && *engine_data_dir) {
        config->user_data_dir = typio_strdup(engine_data_dir);
    }

    if (!config->schema || !config->shared_data_dir || !config->user_data_dir) {
        typio_rime_free_config(config);
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    engine_config = typio_instance_get_engine_config(instance, "rime");
    if (!engine_config) {
        return TYPIO_OK;
    }

    const char *persisted = typio_config_get_string(engine_config, "schema", nullptr);
    if (persisted && *persisted) {
        free(config->schema);
        config->schema = typio_strdup(persisted);
    }

    typio_config_free(engine_config);

    if (!config->schema) {
        typio_rime_free_config(config);
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    return TYPIO_OK;
}
