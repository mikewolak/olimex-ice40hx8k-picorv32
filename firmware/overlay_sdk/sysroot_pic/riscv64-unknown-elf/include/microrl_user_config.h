/**
 * \file            microrl_user_config.h
 * \brief           MicroRL user configuration for hexedit
 */

#ifndef MICRORL_HDR_USER_CONFIG_H
#define MICRORL_HDR_USER_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Command line buffer length */
#define MICRORL_CFG_CMDLINE_LEN               128

/* Maximum number of command tokens (words) */
#define MICRORL_CFG_CMD_TOKEN_NMB             8

/* Prompt string */
#define MICRORL_CFG_PROMPT_STRING             "> "

/* Disable colored prompts (ANSI codes) */
#define MICRORL_CFG_USE_PROMPT_COLOR          0

/* Disable tab completion (not needed for hexedit) */
#define MICRORL_CFG_USE_COMPLETE              0

/* Disable quoting (not needed) */
#define MICRORL_CFG_USE_QUOTING               0

/* Disable echo off (not needed for hex editor) */
#define MICRORL_CFG_USE_ECHO_OFF              0

/* Enable command history */
#define MICRORL_CFG_USE_HISTORY               1

/* History buffer size (256 bytes for embedded) */
#define MICRORL_CFG_RING_HISTORY_LEN          256

/* Print buffer length */
#define MICRORL_CFG_PRINT_BUFFER_LEN          40

/* Enable ESC sequences for arrow keys, HOME, END */
#define MICRORL_CFG_USE_ESC_SEQ               1

/* Don't use sprintf (save memory, use custom conversion) */
#define MICRORL_CFG_USE_LIBC_STDIO            0

/* Use carriage return for cursor movement */
#define MICRORL_CFG_USE_CARRIAGE_RETURN       1

/* Disable Ctrl+C handling (keep existing) */
#define MICRORL_CFG_USE_CTRL_C                0

/* Print prompt on init */
#define MICRORL_CFG_PROMPT_ON_INIT            1

/* Newline symbol */
#define MICRORL_CFG_END_LINE                  "\r\n"

/* Disable command hooks */
#define MICRORL_CFG_USE_COMMAND_HOOKS         0

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* MICRORL_HDR_USER_CONFIG_H */
