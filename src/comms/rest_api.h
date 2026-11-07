/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: DEFERRED. /api/* JSON endpoints for a browser control page
**************************************************************************/

/*
    NOT PLANNED FOR THE FIRST VERSION. The only network interfaces being built
    now are the MCP endpoint and the Wi-Fi provisioning page. This stub is kept so
    the idea has a place to live.

    If it is built later:
        - GET  /api/state  -> FuncGenState as JSON
        - POST /api/state  -> partial update of any setting (same validation and
                              limits as the MCP tools, through FuncGenController)
        - registered on the shared HttpServer, serving src/web/index_html.h
        - share the JSON formatting and argument validation with McpServer so the
          limits can't diverge between the two interfaces
*/
