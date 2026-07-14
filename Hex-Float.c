#include <windows.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shellapi.h>

#define IDI_CALCULATOR 101
#define IDB_ABOUT 1001

typedef union {
    float f;
    uint32_t u;
} FloatUnion;

typedef struct {
    HWND hexEdit;
    HWND floatEdit;
    HWND resultFloat;
    HWND resultHex;
    HWND resultDiv;
    HWND resultMul;
    HWND factorDiv;
    HWND factorMul;
    HWND hexToDecEdit;
    HWND decToHexEdit;
    HWND aboutButton;
} AppData;

static AppData g_app;
static HBRUSH g_editBrush = NULL;
static HBRUSH g_windowBrush = NULL;
static int g_updatingConversion = 0;
static HWND g_aboutWnd = NULL;
static HWND g_aboutLink = NULL;

static uint32_t float_to_hex(float v) {
    FloatUnion x;
    x.f = v;
    return x.u;
}

static float hex_to_float(uint32_t h) {
    FloatUnion x;
    x.u = h;
    return x.f;
}

static void formatear_float(float v, char *out, size_t n) {
    snprintf(out, n, "%.15g", v); // Formatear el número con un maximo de x digitos
    for (char *p = out; *p; ++p) {
        if (*p == '.') *p = ',';
    }
}

static void limpiar_texto(char *out, size_t n) {
    if (n > 0) out[0] = '\0';
}

static void set_control_text(HWND control, const char *text) {
    SetWindowTextA(control, text);
    int len = (int)strlen(text);
    SendMessageA(control, EM_SETSEL, len, len);
}

static void limitar_float(char *s) {
    char t[64] = "";
    int coma = 0;
    int menos = 0;
    int digitosEntero = 0;
    int digitosFraccion = 0;
    int maxFraccion = 15; // Máximo de digitos después de la coma
    int enteroEsCero = 1;

    for (int i = 0; s[i]; ++i) {
        char c = s[i];
        if (c == '-') {
            if (menos || t[0] != '\0') continue;
            menos = 1;
            strcat(t, "-");
        } else if (c == ',' || c == '.') {
            if (coma) continue;
            coma = 1;
            strcat(t, ",");
            if (digitosEntero == 0 || enteroEsCero) {
                maxFraccion = 15; // Permitir hasta x digitos después de la coma si el entero es cero
            } else {
                maxFraccion = 15 - digitosEntero; // Permitir hasta x digitos en total (entero + fraccion)
                if (maxFraccion < 0) maxFraccion = 0;
            }
        } else if (isdigit((unsigned char)c)) {
            if (!coma) {
                if (digitosEntero < 15) {
                    size_t l = strlen(t);
                    t[l] = c;
                    t[l + 1] = '\0';
                    digitosEntero++;
                    if (c != '0') enteroEsCero = 0;
                }
            } else {
                if (digitosFraccion < maxFraccion) {
                    size_t l = strlen(t);
                    t[l] = c;
                    t[l + 1] = '\0';
                    digitosFraccion++;
                }
            }
        }
    }
    strcpy(s, t);
}

static void actualizar_hex(void) {
    char text[64];
    char out[64];
    GetWindowTextA(g_app.hexEdit, text, sizeof(text));

    char cleaned[64] = "";
    int n = 0;
    for (int i = 0; text[i] && n < 8; ++i) {
        char c = toupper((unsigned char)text[i]);
        if (c == '0' || c == '1' || c == '2' || c == '3' || c == '4' || c == '5' || c == '6' || c == '7' || c == '8' || c == '9' ||
            c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'E' || c == 'F') {
            cleaned[n++] = c;
        }
    }
    cleaned[n] = '\0';

    if (strcmp(text, cleaned) != 0) {
        set_control_text(g_app.hexEdit, cleaned);
        GetWindowTextA(g_app.hexEdit, text, sizeof(text));
    }

    if (strlen(text) != 8) {
        set_control_text(g_app.resultFloat, "");
        return;
    }

    unsigned long value = strtoul(text, NULL, 16);
    float f = hex_to_float((uint32_t)value);

    formatear_float(f, out, sizeof(out));

    set_control_text(g_app.resultFloat, out);
    SetWindowTextA(g_app.floatEdit, out);
    }

static void actualizar_float(void) {
    char text[64];
    char out[64];
    GetWindowTextA(g_app.floatEdit, text, sizeof(text));

    char cleaned[64];
    strcpy(cleaned, text);
    for (char *p = cleaned; *p; ++p) {
        if (*p == '.') *p = ',';
    }
    limitar_float(cleaned);

    if (strcmp(text, cleaned) != 0) {
        set_control_text(g_app.floatEdit, cleaned);
        GetWindowTextA(g_app.floatEdit, text, sizeof(text));
    }

    if (text[0] == '\0' || strcmp(text, "-") == 0 || strcmp(text, "-,") == 0 || strcmp(text, ",") == 0) {
        set_control_text(g_app.resultHex, "");
        set_control_text(g_app.resultDiv, "");
        set_control_text(g_app.resultMul, "");
        return;
    }

    char valor[64];
    strcpy(valor, text);
    for (char *p = valor; *p; ++p) if (*p == ',') *p = '.';

    double v = strtod(valor, NULL);
    snprintf(out, sizeof(out), "%08X", float_to_hex((float)v));
    set_control_text(g_app.resultHex, out);

    char opbuf1[64];
    char opbuf2[64];
    /* read factors from small edit controls; default 0.75 if invalid */
    char ftext[64];
    double factorDiv = 0.75;
    double factorMul = 0.75;
    if (GetWindowTextA(g_app.factorDiv, ftext, sizeof(ftext)) && ftext[0] != '\0') {
        for (char *p = ftext; *p; ++p) if (*p == ',') *p = '.';
        factorDiv = strtod(ftext, NULL);
        if (factorDiv == 0.0) factorDiv = 0.75; /* avoid div by zero */
    }
    if (GetWindowTextA(g_app.factorMul, ftext, sizeof(ftext)) && ftext[0] != '\0') {
        for (char *p = ftext; *p; ++p) if (*p == ',') *p = '.';
        factorMul = strtod(ftext, NULL);
        if (factorMul == 0.0) factorMul = 0.75;
    }

    double d1 = v / factorDiv;
    double d2 = v * factorMul;
    snprintf(opbuf1, sizeof(opbuf1), "%08X", float_to_hex((float)d1));
    snprintf(opbuf2, sizeof(opbuf2), "%08X", float_to_hex((float)d2));
    set_control_text(g_app.resultDiv, opbuf1);
    set_control_text(g_app.resultMul, opbuf2);
}

static void actualizar_conversion_hex_decimal(HWND sourceControl) {
    if (g_updatingConversion) return;

    char text[64];
    char out[64];

    if (sourceControl == g_app.hexToDecEdit) {
        GetWindowTextA(g_app.hexToDecEdit, text, sizeof(text));

        char cleaned[64] = "";
        int n = 0;
        for (int i = 0; text[i] && n < 63; ++i) {
            char c = toupper((unsigned char)text[i]);
            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
                cleaned[n++] = c;
            }
        }
        cleaned[n] = '\0';

        if (strcmp(text, cleaned) != 0) {
            g_updatingConversion = 1;
            set_control_text(g_app.hexToDecEdit, cleaned);
            g_updatingConversion = 0;
            GetWindowTextA(g_app.hexToDecEdit, text, sizeof(text));
        }

        if (cleaned[0] == '\0') {
            g_updatingConversion = 1;
            set_control_text(g_app.decToHexEdit, "");
            g_updatingConversion = 0;
        } else {
            unsigned long long value = strtoull(cleaned, NULL, 16);
            snprintf(out, sizeof(out), "%llu", value);
            g_updatingConversion = 1;
            set_control_text(g_app.decToHexEdit, out);
            g_updatingConversion = 0;
        }
    } else if (sourceControl == g_app.decToHexEdit) {
        GetWindowTextA(g_app.decToHexEdit, text, sizeof(text));

        char cleaned[64] = "";
        int n = 0;
        for (int i = 0; text[i] && n < 63; ++i) {
            char c = text[i];
            if ((c >= '0' && c <= '9') || c == '-') {
                cleaned[n++] = c;
            }
        }
        cleaned[n] = '\0';

        if (strcmp(text, cleaned) != 0) {
            g_updatingConversion = 1;
            set_control_text(g_app.decToHexEdit, cleaned);
            g_updatingConversion = 0;
            GetWindowTextA(g_app.decToHexEdit, text, sizeof(text));
        }

        if (cleaned[0] == '\0') {
            g_updatingConversion = 1;
            set_control_text(g_app.hexToDecEdit, "");
            g_updatingConversion = 0;
        } else {
            char *end = NULL;
            long long value = strtoll(cleaned, &end, 10);
            if (end != NULL && *end == '\0') {
                snprintf(out, sizeof(out), "%llX", value);
                g_updatingConversion = 1;
                set_control_text(g_app.hexToDecEdit, out);
                g_updatingConversion = 0;
            } else {
                g_updatingConversion = 1;
                set_control_text(g_app.hexToDecEdit, "");
                g_updatingConversion = 0;
            }
        }
    }
}

static void crear_control(HWND parent, int x, int y, const char *text, DWORD style, HWND *out) {
    *out = CreateWindowExA(0, "EDIT", text, WS_CHILD | WS_VISIBLE | style, x, y, 160, 21, parent, NULL, NULL, NULL); // Tamaño rectangulos para escribir ancho/alto
    HFONT font = CreateFontA(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             FIXED_PITCH | FF_MODERN, "Consolas");
    SendMessageA(*out, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(*out, EM_SETLIMITTEXT, 32, 0);
}
static void crear_control_arial(HWND parent, int x, int y, const char *text, DWORD style, HWND *out)
{
    *out = CreateWindowExA(
        0,
        "EDIT",
        text,
        WS_CHILD | WS_VISIBLE | style,
        x, y, 160, 21, // Tamaño rectangulos
        parent, NULL, NULL, NULL);

    HFONT font = CreateFontA(
        16, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        "Arial");

    SendMessageA(*out, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(*out, EM_SETLIMITTEXT, 32, 0);
}

static void crear_control_pequeno(HWND parent, int x, int y, const char *text, DWORD style, HWND *out) {
    *out = CreateWindowExA(0, "EDIT", text, WS_CHILD | WS_VISIBLE | style, x, y, 70, 21, parent, NULL, NULL, NULL); // Tamaño rectangulos 0.75 para escribir ancho/alto
    HFONT font = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             FIXED_PITCH | FF_MODERN, "Arial");
    SendMessageA(*out, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(*out, EM_SETLIMITTEXT, 16, 0);
}

static void crear_label(HWND parent, int x, int y, const char *text) { // Tamaño fuente para texto
    int len = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
    wchar_t *wtext = (wchar_t *)malloc(len * sizeof(wchar_t));
    MultiByteToWideChar(CP_ACP, 0, text, -1, wtext, len);
    
    HWND label = CreateWindowExW(0, L"STATIC", wtext, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, 90, 28, parent, NULL, NULL, NULL); // Tamaño fondo de texto Hexadecimal y Decimal
    HFONT font = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Segoe UI");
    SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
    
    free(wtext);
}

static void crear_label_wide(HWND parent, int x, int y, LPCWSTR text) { // Tamaño fuente para texto signo dividir
    HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_CENTER, x, y, 90, 40, parent, NULL, NULL, NULL);
    HFONT font = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Segoe UI");
    SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
}

static void crear_resultado(HWND parent, int x, int y, HWND *out) {
    *out = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_READONLY | ES_CENTER, x, y, 160, 21, parent, NULL, NULL, NULL); // Tamaño rectangulos fijos ancho/alto
    HFONT font = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             FIXED_PITCH | FF_MODERN, "Consolas");
    SendMessageA(*out, WM_SETFONT, (WPARAM)font, TRUE);
}

static void crear_resultado_arial(HWND parent, int x, int y, HWND *out) {
    *out = CreateWindowExA(
        0,
        "EDIT",
        "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_READONLY | ES_CENTER,
        x, y, 160, 21, // Tamaño rectangulos
        parent, NULL, NULL, NULL);

    HFONT font = CreateFontA(
        16, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,
        FIXED_PITCH | FF_MODERN,
        "Arial");

    SendMessageA(*out, WM_SETFONT, (WPARAM)font, TRUE);
}

static void mostrar_about(HWND owner) { // 
    if (g_aboutWnd != NULL) {
        SetForegroundWindow(g_aboutWnd);
        return;
    }

    RECT rc;
    GetWindowRect(owner, &rc);
    int width = 280;
    int height = 130;
    int x = rc.left + ((rc.right - rc.left - width) / 2);
    int y = rc.top + ((rc.bottom - rc.top - height) / 2);

    g_aboutWnd = CreateWindowExA(0, "AboutWindow", "Acerca del programa", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                 x, y, width, height, owner, NULL, NULL, NULL);
    if (g_aboutWnd == NULL) return;

    CreateWindowExA(0, "STATIC", "Hex-Float IEEE-754 por PeterDelta", WS_CHILD | WS_VISIBLE | SS_CENTER,
                    10, 10, 260, 20, g_aboutWnd, NULL, NULL, NULL);
    HWND link = CreateWindowExA(0, "STATIC", "v0.91 github link", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY,
                                60, 35, 150, 20, g_aboutWnd, NULL, NULL, NULL);
    g_aboutLink = link;
    SetWindowLongPtrA(link, GWLP_USERDATA, (LONG_PTR)"https://github.com/PeterDelta/Hex-Float");
    CreateWindowExA(0, "BUTTON", "Cerrar", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    100, 70, 60, 20, g_aboutWnd, (HMENU)1, NULL, NULL);
    ShowWindow(g_aboutWnd, SW_SHOW);
    UpdateWindow(g_aboutWnd);
}

static LRESULT CALLBACK AboutWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == 1 && HIWORD(wParam) == BN_CLICKED) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (HIWORD(wParam) == STN_CLICKED && (HWND)lParam == g_aboutLink) {
                const char *url = (const char *)GetWindowLongPtrA(g_aboutLink, GWLP_USERDATA);
                if (url != NULL) {
                    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
                }
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC: {
            if ((HWND)lParam != NULL) {
                HDC hdc = (HDC)wParam;
                SetTextColor(hdc, RGB(0, 0, 0));
                SetBkMode(hdc, TRANSPARENT);
                return (LRESULT)GetStockObject(HOLLOW_BRUSH);
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (g_aboutWnd == hwnd) g_aboutWnd = NULL;
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void aplicar_icono(HWND hwnd) {
    HICON icon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDI_CALCULATOR));
    if (icon == NULL) {
        icon = LoadIconA(NULL, IDI_APPLICATION);
    }
    if (icon != NULL) {
        SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    }
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            aplicar_icono(hwnd);
            crear_label_wide(hwnd, 50, 2, L"Hex → Float"); // Etiqueta para el formato hexadecimal
            crear_control(hwnd, 10, 22, "", WS_BORDER | ES_CENTER, &g_app.hexEdit); // Control para ingresar el valor en formato hexadecimal
            crear_resultado_arial(hwnd, 10, 46, &g_app.resultFloat); // Resultado de la conversión a float

            crear_label_wide(hwnd, 214, 2, L"Float → Hex"); // Etiqueta para el formato float - signo → ⇒
            crear_control_arial(hwnd, 180, 22, "", WS_BORDER | ES_CENTER, &g_app.floatEdit); // Control para ingresar el valor en formato float
            crear_resultado(hwnd, 180, 46, &g_app.resultHex); // Resultado de la conversión a hexadecimal
            crear_label_wide(hwnd, 46, 70, L"÷"); // Etiqueta para el factor de división - signo ÷
            crear_control_pequeno(hwnd, 100, 70, "0.75", WS_BORDER | ES_CENTER, &g_app.factorDiv); // Mueve izq. o dcha. el rectangulo para escribir el factor de división
            crear_resultado(hwnd, 180, 70, &g_app.resultDiv); // Resultado de la división
            crear_label_wide(hwnd, 46, 94, L"×"); // Etiqueta para el factor de multiplicación - signo ×
            crear_control_pequeno(hwnd, 100, 94, "0.75", WS_BORDER | ES_CENTER, &g_app.factorMul); // Mueve izq. o dcha. el rectangulo para escribir el factor de multiplicación
            crear_resultado(hwnd, 180, 94, &g_app.resultMul); // Resultado de la multiplicación

            crear_label(hwnd, 86, 124, "Hexadecimal:");
            crear_control(hwnd, 180, 124, "", WS_BORDER | ES_CENTER, &g_app.hexToDecEdit);
            crear_label(hwnd, 118, 148, "Decimal:");
            crear_control_arial(hwnd, 180, 148, "", WS_BORDER | ES_CENTER, &g_app.decToHexEdit);
            g_app.aboutButton = CreateWindowExA(0, "BUTTON", "Acerca de", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                               10, 148, 60, 20, hwnd, (HMENU)IDB_ABOUT, NULL, NULL);
            SendMessageA(g_app.aboutButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SetFocus(g_app.hexEdit);
            return 0;
        }
        
        case WM_COMMAND: {
            if (LOWORD(wParam) == IDB_ABOUT && HIWORD(wParam) == BN_CLICKED) {
                mostrar_about(hwnd);
                return 0;
            }
            if (HIWORD(wParam) == EN_CHANGE) {
                if ((HWND)lParam == g_app.hexEdit) {
                    actualizar_hex();
                } else if ((HWND)lParam == g_app.floatEdit || (HWND)lParam == g_app.factorDiv || (HWND)lParam == g_app.factorMul) {
                    actualizar_float();
                } else if ((HWND)lParam == g_app.hexToDecEdit || (HWND)lParam == g_app.decToHexEdit) {
                    actualizar_conversion_hex_decimal((HWND)lParam);
                }
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            SetTextColor((HDC)wParam, RGB(0, 0, 0));
            SetBkColor((HDC)wParam, RGB(242, 246, 255));
            return (LRESULT)g_editBrush;
        }
        case WM_CTLCOLORSTATIC: {
            SetTextColor((HDC)wParam, RGB(0, 0, 0)); // Cambiar el color del texto
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (LRESULT)g_windowBrush;
        }
        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcA(hwnd, msg, wParam, lParam);
            if (hit == HTCLIENT) return HTCAPTION;
            return hit;
        }
        case WM_KEYDOWN: {
            if ((GetKeyState(VK_CONTROL) & 0x8000) && (LOWORD(wParam) == 'A' || LOWORD(wParam) == 'a')) {
                HWND focus = GetFocus();
                if (focus == g_app.hexEdit || focus == g_app.floatEdit || focus == g_app.resultFloat || focus == g_app.resultHex || focus == g_app.resultDiv || focus == g_app.resultMul || focus == g_app.factorDiv || focus == g_app.factorMul || focus == g_app.hexToDecEdit || focus == g_app.decToHexEdit) {
                    SendMessageA(focus, EM_SETSEL, 0, -1);
                    return 0;
                }
            }
            break;
        }
        case WM_DESTROY:
            if (g_editBrush != NULL) {
                DeleteObject(g_editBrush);
                g_editBrush = NULL;
            }
            if (g_windowBrush != NULL) {
                DeleteObject(g_windowBrush);
                g_windowBrush = NULL;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_editBrush = CreateSolidBrush(RGB(242, 246, 255)); // Color de fondo de los cuadros de texto
    g_windowBrush = CreateSolidBrush(RGB(200, 200, 200)); // Color de fondo de la ventana

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)g_windowBrush; // Color de fondo de la ventana
    wc.hIcon = LoadIconA(NULL, IDI_WINLOGO);
    wc.hIconSm = LoadIconA(NULL, IDI_WINLOGO);
    wc.lpszClassName = "IEEE754Window";
    RegisterClassExA(&wc);

    WNDCLASSEXA aboutClass = {0};
    aboutClass.cbSize = sizeof(aboutClass);
    aboutClass.lpfnWndProc = AboutWindowProc;
    aboutClass.hInstance = hInstance;
    aboutClass.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    aboutClass.lpszClassName = "AboutWindow";
    RegisterClassExA(&aboutClass);

    const int width = 359; // Tamaño de la ventana ancho
    const int height = 210; // Tamaño de la ventana alto
    int x = GetSystemMetrics(SM_CXSCREEN) - width - 15; // Posición horizontal de la ventana (20 píxeles desde el borde derecho)
    int y = 10; // Posición vertical de la ventana (20 píxeles desde el borde superior)
    if (x < 0) x = 0; // Asegurarse de que la ventana esté dentro de la pantalla
    if (y < 0) y = 0; // Asegurarse de que la ventana esté dentro de la pantalla

    HWND hwnd = CreateWindowExA(0, "IEEE754Window", "Hex-Float", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                x, y, width, height, NULL, NULL, hInstance, NULL); // Tamaño de la ventana ancho/alto
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
