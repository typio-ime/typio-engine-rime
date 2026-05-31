#include "rime_internal.h"
#include <curl/curl.h>
#include <sys/wait.h>

#define RIME_ICE_URL \
    "https://github.com/iDvel/rime-ice/releases/download/nightly/full.zip"

static TypioResult download_zip(const char *url, const char *dest_path) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return TYPIO_ERROR;
    }

    FILE *fp = fopen(dest_path, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return TYPIO_ERROR;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "typio-rime-setup/1.0");

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    fclose(fp);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        unlink(dest_path);
        typio_log_error("rime: download failed: %s", curl_easy_strerror(res));
        return TYPIO_ERROR;
    }
    if (http_code >= 400) {
        unlink(dest_path);
        typio_log_error("rime: download failed: HTTP %ld", http_code);
        return TYPIO_ERROR;
    }
    return TYPIO_OK;
}

static TypioResult extract_zip(const char *zip_path, const char *dest_dir) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("unzip", "unzip", "-o", "-q", zip_path, "-d", dest_dir,
               (char *)NULL);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        typio_log_error("rime: extraction failed (unzip exit %d)",
                        WEXITSTATUS(status));
        return TYPIO_ERROR;
    }
    return TYPIO_OK;
}

TypioResult typio_rime_setup_rime_ice(const char *user_data_dir) {
    char marker_path[512];
    snprintf(marker_path, sizeof(marker_path), "%s/.typio-rime-ice-installed",
             user_data_dir);

    struct stat st;
    if (stat(marker_path, &st) == 0) {
        typio_log_info("rime: rime-ice already installed in %s",
                       user_data_dir);
        return TYPIO_OK;
    }

    if (!typio_rime_ensure_dir(user_data_dir)) {
        return TYPIO_ERROR;
    }

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s/.rime-ice-download.zip",
             user_data_dir);

    typio_log_info("rime: downloading rime-ice (full.zip ~16MB)...");
    TypioResult res = download_zip(RIME_ICE_URL, tmp_path);
    if (res != TYPIO_OK) {
        return res;
    }

    typio_log_info("rime: extracting rime-ice to %s", user_data_dir);
    res = extract_zip(tmp_path, user_data_dir);
    unlink(tmp_path);

    if (res != TYPIO_OK) {
        return res;
    }

    FILE *mf = fopen(marker_path, "w");
    if (mf) {
        fputs("rime-ice", mf);
        fclose(mf);
    }

    typio_log_info("rime: rime-ice installed successfully");
    return TYPIO_OK;
}
