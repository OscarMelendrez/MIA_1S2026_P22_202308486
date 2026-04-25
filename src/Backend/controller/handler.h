#pragma once

namespace crow {
    struct request;
    struct response;
}

void EjecutarScriptHandler(const crow::request& req, crow::response& res);
void MountedHandler       (const crow::request& req, crow::response& res);
void CorsPreflightHandler (const crow::request& req, crow::response& res);
void ReportesHandler      (const crow::request& req, crow::response& res,
                           const std::string& filename);