#include "export.h"
#include "util/str.h"
#include "util/fs.h"
#include "util/log.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

ExportConfig export_config_defaults(void) {
    ExportConfig c = {0};
    c.include_thinking = true;
    c.include_tool_calls = true;
    c.include_timestamps = true;
    c.dark_theme = true;
    c.title = NULL;
    return c;
}

/* ---- HTML escaping ---- */

static void html_escape_into(Str *out, const char *text) {
    if (!text) return;
    for (const char *p = text; *p; p++) {
        switch (*p) {
            case '&':  str_append(out, "&amp;");  break;
            case '<':  str_append(out, "&lt;");   break;
            case '>':  str_append(out, "&gt;");   break;
            case '"':  str_append(out, "&quot;");  break;
            case '\'': str_append(out, "&#39;");  break;
            default:   str_append_char(out, *p);  break;
        }
    }
}

/* ---- CSS generation ---- */

static void emit_css(Str *out, bool dark) {
    const char *bg    = dark ? "#1e1e2e" : "#ffffff";
    const char *fg    = dark ? "#cdd6f4" : "#1e1e2e";
    const char *user_bg = dark ? "#1e66f5" : "#dce5ff";
    const char *user_fg = dark ? "#ffffff" : "#1e1e2e";
    const char *asst_bg = dark ? "#313244" : "#f0f0f0";
    const char *asst_fg = dark ? "#cdd6f4" : "#1e1e2e";
    const char *code_bg = dark ? "#181825" : "#f5f5f5";
    const char *border  = dark ? "#45475a" : "#cccccc";
    const char *dim     = dark ? "#6c7086" : "#888888";
    const char *header_bg = dark ? "#11111b" : "#f8f8f8";

    str_appendf(out,
        "<style>\n"
        "* { box-sizing: border-box; margin: 0; padding: 0; }\n"
        "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;\n"
        "  background: %s; color: %s; line-height: 1.6; }\n"
        ".header { background: %s; border-bottom: 1px solid %s;\n"
        "  padding: 16px 24px; }\n"
        ".header h1 { font-size: 1.4em; margin-bottom: 4px; }\n"
        ".header .meta { font-size: 0.85em; color: %s; }\n"
        ".messages { max-width: 900px; margin: 0 auto; padding: 24px; }\n"
        ".msg { margin-bottom: 16px; padding: 12px 16px; border-radius: 12px;\n"
        "  max-width: 80%%; word-wrap: break-word; white-space: pre-wrap; }\n"
        ".msg-user { background: %s; color: %s; margin-left: auto; text-align: left; }\n"
        ".msg-assistant { background: %s; color: %s; margin-right: auto; }\n"
        ".msg-tool { background: %s; color: %s; margin-right: auto;\n"
        "  border-left: 3px solid %s; font-size: 0.9em; }\n"
        ".timestamp { font-size: 0.75em; color: %s; margin-bottom: 4px; }\n"
        "code { font-family: 'SFMono-Regular', Consolas, 'Liberation Mono', Menlo, monospace; }\n"
        "pre { background: %s; padding: 12px; border-radius: 6px;\n"
        "  overflow-x: auto; margin: 8px 0; }\n"
        "pre code { font-size: 0.9em; }\n"
        "details { margin: 8px 0; }\n"
        "summary { cursor: pointer; font-weight: 600; font-size: 0.9em;\n"
        "  color: %s; padding: 4px 0; }\n"
        "details .content { padding: 8px 12px; border-left: 2px solid %s;\n"
        "  margin-top: 4px; font-size: 0.9em; }\n"
        "@media (max-width: 600px) {\n"
        "  .messages { padding: 12px; }\n"
        "  .msg { max-width: 95%%; }\n"
        "}\n"
        "</style>\n",
        bg, fg, header_bg, border, dim,
        user_bg, user_fg, asst_bg, asst_fg,
        asst_bg, asst_fg, border, dim, code_bg, dim, border);
}

/* ---- Timestamp formatting ---- */

static void emit_timestamp(Str *out, int64_t ts_ms) {
    time_t secs = (time_t)(ts_ms / 1000);
    struct tm tm_buf;
    struct tm *tm = localtime_r(&secs, &tm_buf);
    if (!tm) return;
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    str_appendf(out, "<div class=\"timestamp\">%s</div>\n", buf);
}

/* ---- Text with basic code-block detection ---- */

static void emit_text_content(Str *out, const char *text) {
    if (!text) return;

    const char *p = text;
    while (*p) {
        const char *fence = strstr(p, "```");
        if (!fence) {
            html_escape_into(out, p);
            break;
        }
        /* emit text before fence */
        if (fence > p) {
            char *before = strndup(p, (size_t)(fence - p));
            if (before) {
                html_escape_into(out, before);
                free(before);
            }
        }
        /* skip opening ``` and optional lang tag */
        const char *after_fence = fence + 3;
        const char *eol = strchr(after_fence, '\n');
        if (!eol) {
            html_escape_into(out, fence);
            break;
        }
        str_append(out, "<pre><code>");
        const char *close = strstr(eol + 1, "```");
        if (close) {
            char *code = strndup(eol + 1, (size_t)(close - eol - 1));
            if (code) {
                html_escape_into(out, code);
                free(code);
            }
            str_append(out, "</code></pre>");
            p = close + 3;
            if (*p == '\n') p++;
        } else {
            /* unterminated — emit rest as code */
            html_escape_into(out, eol + 1);
            str_append(out, "</code></pre>");
            break;
        }
    }
}

/* ---- Render a single message entry ---- */

static void render_message(Str *out, SessionEntry *entry, const ExportConfig *cfg) {
    if (!entry || !entry->data) return;

    cJSON *role_json = cJSON_GetObjectItem(entry->data, "role");
    const char *role = (role_json && cJSON_IsString(role_json)) ? role_json->valuestring : "unknown";

    const char *css_class = "msg-assistant";
    const char *label = "Assistant";
    if (strcmp(role, "user") == 0) {
        css_class = "msg-user";
        label = "User";
    } else if (strcmp(role, "tool_result") == 0) {
        css_class = "msg-tool";
        label = "Tool Result";
    }

    str_appendf(out, "<div class=\"msg %s\">\n", css_class);

    if (cfg->include_timestamps && entry->timestamp > 0) {
        emit_timestamp(out, entry->timestamp);
    }

    str_appendf(out, "<strong>%s</strong><br>\n", label);

    cJSON *content = cJSON_GetObjectItem(entry->data, "content");
    if (content && cJSON_IsArray(content)) {
        int n = cJSON_GetArraySize(content);
        for (int i = 0; i < n; i++) {
            cJSON *block = cJSON_GetArrayItem(content, i);
            cJSON *type = cJSON_GetObjectItem(block, "type");
            if (!type || !cJSON_IsString(type)) continue;

            if (strcmp(type->valuestring, "text") == 0) {
                cJSON *txt = cJSON_GetObjectItem(block, "text");
                if (txt && cJSON_IsString(txt)) {
                    emit_text_content(out, txt->valuestring);
                }
            } else if (strcmp(type->valuestring, "thinking") == 0 && cfg->include_thinking) {
                cJSON *thinking = cJSON_GetObjectItem(block, "thinking");
                if (thinking && cJSON_IsString(thinking)) {
                    str_append(out,
                        "<details><summary>Thinking</summary>\n"
                        "<div class=\"content\">");
                    html_escape_into(out, thinking->valuestring);
                    str_append(out, "</div></details>\n");
                }
            } else if (strcmp(type->valuestring, "tool_call") == 0 && cfg->include_tool_calls) {
                cJSON *name = cJSON_GetObjectItem(block, "name");
                cJSON *args = cJSON_GetObjectItem(block, "arguments");
                const char *tool_name = (name && cJSON_IsString(name)) ? name->valuestring : "unknown";
                str_appendf(out,
                    "<details><summary>Tool: ");
                html_escape_into(out, tool_name);
                str_append(out, "</summary>\n<div class=\"content\"><pre><code>");
                if (args) {
                    char *args_str = cJSON_Print(args);
                    if (args_str) {
                        html_escape_into(out, args_str);
                        free(args_str);
                    }
                }
                str_append(out, "</code></pre></div></details>\n");
            }
        }
    }

    str_append(out, "</div>\n");
}

/* ---- Main export ---- */

char *session_export_html_string(Session *s, ExportConfig *config) {
    if (!s) return NULL;

    ExportConfig defaults = export_config_defaults();
    const ExportConfig *cfg = config ? config : &defaults;

    const char *title_str = cfg->title;
    if (!title_str || !title_str[0]) {
        title_str = session_get_name(s);
    }
    if (!title_str || !title_str[0]) {
        title_str = s->session_id ? s->session_id : "Session Export";
    }

    Str html = str_new(4096);

    str_append(&html, "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
        "<meta charset=\"UTF-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "<title>");
    html_escape_into(&html, title_str);
    str_append(&html, "</title>\n");

    emit_css(&html, cfg->dark_theme);
    str_append(&html, "</head>\n<body>\n");

    /* Header */
    str_append(&html, "<div class=\"header\">\n<h1>");
    html_escape_into(&html, title_str);
    str_append(&html, "</h1>\n<div class=\"meta\">");
    str_appendf(&html, "Session: %s", s->session_id ? s->session_id : "unknown");
    str_appendf(&html, " | Entries: %d", s->entry_count);

    /* Find model info from entries */
    for (int i = 0; i < s->entry_count; i++) {
        if (s->entries[i].type == ENTRY_MESSAGE && s->entries[i].data) {
            cJSON *model = cJSON_GetObjectItem(s->entries[i].data, "model_id");
            if (model && cJSON_IsString(model)) {
                str_appendf(&html, " | Model: ");
                html_escape_into(&html, model->valuestring);
                break;
            }
        }
    }

    /* Date */
    if (s->entry_count > 0 && s->entries[0].timestamp > 0) {
        time_t secs = (time_t)(s->entries[0].timestamp / 1000);
        struct tm tm_buf;
        struct tm *tm = localtime_r(&secs, &tm_buf);
        if (tm) {
            char date_buf[64];
            strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm);
            str_appendf(&html, " | Date: %s", date_buf);
        }
    }

    str_append(&html, "</div>\n</div>\n");

    /* Messages */
    str_append(&html, "<div class=\"messages\">\n");
    for (int i = 0; i < s->entry_count; i++) {
        if (s->entries[i].type == ENTRY_MESSAGE) {
            render_message(&html, &s->entries[i], cfg);
        }
    }
    str_append(&html, "</div>\n</body>\n</html>\n");

    return str_take(&html);
}

int session_export_html(Session *s, const char *output_path, ExportConfig *config) {
    if (!s || !output_path) return -1;

    char *html = session_export_html_string(s, config);
    if (!html) return -1;

    int rc = fs_write_file(output_path, html, strlen(html));
    free(html);
    return rc;
}
