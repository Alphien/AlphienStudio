/*
 * ServerMain.cpp
 *
 * Copyright (C) 2009-16 by RStudio, Inc.
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */


#include <pthread.h>
#include <signal.h>

#include <core/Error.hpp>
#include <core/LogWriter.hpp>
#include <core/ProgramStatus.hpp>
#include <core/ProgramOptions.hpp>

#include <core/text/TemplateFilter.hpp>

#include <core/system/PosixSystem.hpp>
#include <core/system/Crypto.hpp>

#include <core/http/URL.hpp>
#include <core/http/AsyncUriHandler.hpp>
#include <server_core/http/SecureCookie.hpp>
#include <core/http/TcpIpAsyncServer.hpp>

#include <core/gwt/GwtLogHandler.hpp>
#include <core/gwt/GwtFileHandler.hpp>

#include <monitor/MonitorClient.hpp>

#include <session/SessionConstants.hpp>


#include <server/auth/ServerAuthHandler.hpp>
#include <server/auth/ServerValidateUser.hpp>
#include <server/auth/ServerSecureUriHandler.hpp>

#include <server/ServerOptions.hpp>
#include <server/ServerUriHandlers.hpp>
#include <server/ServerScheduler.hpp>
#include <server/ServerSessionProxy.hpp>
#include <server/ServerSessionManager.hpp>
#include <server/ServerProcessSupervisor.hpp>

#include "ServerAddins.hpp"
#include "ServerBrowser.hpp"
#include "ServerEval.hpp"
#include "ServerInit.hpp"
#include "ServerMeta.hpp"
#include "ServerOffline.hpp"
#include "ServerPAMAuth.hpp"
#include "ServerREnvironment.hpp"

using namespace rstudio;
using namespace rstudio::core;
using namespace server;

// forward-declare overlay methods
namespace rstudio {
namespace server {
namespace overlay {

Error initialize();
Error startup();
bool reloadConfiguration();
void shutdown();
bool requireLocalR();

} // namespace overlay
} // namespace server
} // namespace rstudio

namespace {
   
bool mainPageFilter(const core::http::Request& request,
                    core::http::Response* pResponse)
{
   return server::eval::expirationFilter(request, pResponse) &&
          server::browser::supportedBrowserFilter(request, pResponse) &&
          auth::handler::mainPageFilter(request, pResponse);
}


http::UriHandlerFunction blockingFileHandler()
{
   Options& options = server::options();

   // determine initJs (none for now)
   std::string initJs;

   // return file
   return gwt::fileHandlerFunction(options.wwwLocalPath(),
                                   "/",
                                   mainPageFilter,
                                   initJs,
                                   options.gwtPrefix(),
                                   options.wwwUseEmulatedStack(),
                                   options.wwwFrameOrigin());
}

//
// some fancy footwork is required to take the standand blocking file handler
// and make it work within a secure async context.
//
auth::SecureAsyncUriHandlerFunction secureAsyncFileHandler()
{
   // create a functor which can adapt a synchronous file handler into
   // an asynchronous handler
   class FileRequestHandler {
   public:
      static void handleRequest(
            const http::UriHandlerFunction& fileHandlerFunction,
            boost::shared_ptr<http::AsyncConnection> pConnection)
      {
         fileHandlerFunction(pConnection->request(), &(pConnection->response()));
         pConnection->writeResponse();
      }
   };

   // use this functor to generate an async uri handler function from the
   // stock blockingFileHandler (defined above)
   http::AsyncUriHandlerFunction asyncFileHandler =
      boost::bind(FileRequestHandler::handleRequest, blockingFileHandler(), _1);


   // finally, adapt this to be a secure async uri handler by binding out the
   // first parameter (username, which the gwt file handler knows nothing of)
   return boost::bind(asyncFileHandler, _2);
}

// http server
boost::shared_ptr<http::AsyncServer> s_pHttpServer;

Error httpServerInit()
{
   s_pHttpServer.reset(server::httpServerCreate());

   // set server options
   s_pHttpServer->setAbortOnResourceError(true);
   s_pHttpServer->setScheduledCommandInterval(
                                    boost::posix_time::milliseconds(500));

   // initialize
   return server::httpServerInit(s_pHttpServer.get());
}

void pageNotFoundHandler(const http::Request& request,
                         http::Response* pResponse)
{
   std::ostringstream os;
   std::map<std::string, std::string> vars;
   vars["request_uri"] = string_utils::jsLiteralEscape(request.uri());

   FilePath notFoundTemplate = FilePath(options().wwwLocalPath()).childPath("404.htm");
   core::Error err = core::text::renderTemplate(notFoundTemplate, vars, os);

   if (err)
   {
      // if we cannot display the 404 page log the error
      // note: this should never happen in a proper deployment
      LOG_ERROR(err);
   }
   else
   {
      std::string body = os.str();
      pResponse->setContentType("text/html");
      pResponse->setBodyUnencoded(body);
   }

   // set 404 status even if there was an error showing the proper not found page
   pResponse->setStatusCode(core::http::status::NotFound);
}

void httpServerAddHandlers()
{
   // establish json-rpc handlers
   using namespace server::auth;
   using namespace server::session_proxy;
   uri_handlers::add("/rpc", secureAsyncJsonRpcHandler(proxyRpcRequest));
   uri_handlers::add("/events", secureAsyncJsonRpcHandler(proxyEventsRequest));

   // establish content handlers
   uri_handlers::add("/graphics", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/upload", secureAsyncUploadHandler(proxyContentRequest));
   uri_handlers::add("/export", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/source", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/content", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/diff", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/file_show", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/view_pdf", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/agreement", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/presentation", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/pdf_js", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/mathjax", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/connections", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/theme", secureAsyncHttpHandler(proxyContentRequest));
   uri_handlers::add("/python", secureAsyncHttpHandler(proxyContentRequest));

   // content handlers which might be accessed outside the context of the
   // workbench get secure + authentication when required
   uri_handlers::add("/help", secureAsyncHttpHandler(proxyContentRequest, true));
   uri_handlers::add("/files", secureAsyncHttpHandler(proxyContentRequest, true));
   uri_handlers::add("/custom", secureAsyncHttpHandler(proxyContentRequest, true));
   uri_handlers::add("/session", secureAsyncHttpHandler(proxyContentRequest, true));
   uri_handlers::add("/docs", secureAsyncHttpHandler(secureAsyncFileHandler(), true));
   uri_handlers::add("/html_preview", secureAsyncHttpHandler(proxyContentRequest, true));
   uri_handlers::add("/rmd_output", secureAsyncHttpHandler(proxyContentRequest, true));
   uri_handlers::add("/grid_data", secureAsyncHttpHandler(proxyContentRequest, true));
   uri_handlers::add("/grid_resource", secureAsyncHttpHandler(proxyContentRequest, true));
   uri_handlers::add("/chunk_output", secureAsyncHttpHandler(proxyContentRequest, true)); 
   uri_handlers::add("/profiles", secureAsyncHttpHandler(proxyContentRequest, true));
   uri_handlers::add("/rmd_data", secureAsyncHttpHandler(proxyContentRequest, true));
   uri_handlers::add("/profiler_resource", secureAsyncHttpHandler(proxyContentRequest, true));

   // proxy localhost if requested
   if (server::options().wwwProxyLocalhost())
   {
      uri_handlers::addProxyHandler("/p/", secureAsyncHttpHandler(
                                       boost::bind(proxyLocalhostRequest, false, _1, _2), true));
      uri_handlers::addProxyHandler("/p6/", secureAsyncHttpHandler(
                                       boost::bind(proxyLocalhostRequest, true, _1, _2), true));
   }

   // establish logging handler
   uri_handlers::addBlocking("/log", secureJsonRpcHandler(gwt::handleLogRequest));

   // establish meta
   uri_handlers::addBlocking("/meta", secureJsonRpcHandler(meta::handleMetaRequest));

   // establish progress handler
   FilePath wwwPath(server::options().wwwLocalPath());
   FilePath progressPagePath = wwwPath.complete("progress.htm");
   uri_handlers::addBlocking("/progress",
                               secureHttpHandler(boost::bind(
                               core::text::handleSecureTemplateRequest,
                               _1, progressPagePath, _2, _3)));

   // establish browser unsupported handler
   using namespace server::browser;
   uri_handlers::addBlocking(kBrowserUnsupported,
                             handleBrowserUnsupportedRequest);

   // restrct access to templates directory
   uri_handlers::addBlocking("/templates", pageNotFoundHandler);

   // initialize gwt symbol maps
   gwt::initializeSymbolMaps(server::options().wwwSymbolMapsPath());

   // add default handler for gwt app
   uri_handlers::setBlockingDefault(blockingFileHandler());
}

void reloadConfiguration()
{
   // swallow the output for now
   // open source currently has no configuration reload options
   // so displaying it as successful would be confusing to those users
   // as no action would have occurred
   overlay::reloadConfiguration();
}

// bogus SIGCHLD handler (never called)
void handleSIGCHLD(int)
{
}

// wait for and handle signals
Error waitForSignals()
{
   // setup bogus handler for SIGCHLD (if we don't do this then
   // we can't successfully block/wait for the signal). This also
   // allows us to specify SA_NOCLDSTOP
   struct sigaction sa;
   ::memset(&sa, 0, sizeof sa);
   sa.sa_handler = handleSIGCHLD;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = SA_NOCLDSTOP;
   int result = ::sigaction(SIGCHLD, &sa, NULL);
   if (result != 0)
      return systemError(errno, ERROR_LOCATION);

   // block signals that we want to sigwait on
   sigset_t wait_mask;
   sigemptyset(&wait_mask);
   sigaddset(&wait_mask, SIGCHLD);
   sigaddset(&wait_mask, SIGINT);
   sigaddset(&wait_mask, SIGQUIT);
   sigaddset(&wait_mask, SIGTERM);
   sigaddset(&wait_mask, SIGHUP);

   result = ::pthread_sigmask(SIG_BLOCK, &wait_mask, NULL);
   if (result != 0)
      return systemError(result, ERROR_LOCATION);

   // wait for child exits
   for(;;)
   {
      // perform wait
      int sig = 0;
      int result = ::sigwait(&wait_mask, &sig);
      if (result != 0)
         return systemError(result, ERROR_LOCATION);

      // SIGCHLD
      if (sig == SIGCHLD)
      {
         sessionManager().notifySIGCHLD();
      }

      // SIGINT, SIGQUIT, SIGTERM
      else if (sig == SIGINT || sig == SIGQUIT || sig == SIGTERM)
      {
         //
         // Here is where we can perform server cleanup e.g.
         // closing pam sessions
         //

         // call overlay shutdown
         overlay::shutdown();

         // clear the signal mask
         Error error = core::system::clearSignalMask();
         if (error)
            LOG_ERROR(error);

         // reset the signal to its default
         struct sigaction sa;
         ::memset(&sa, 0, sizeof sa);
         sa.sa_handler = SIG_DFL;
         int result = ::sigaction(sig, &sa, NULL);
         if (result != 0)
            LOG_ERROR(systemError(result, ERROR_LOCATION));

         // re-raise the signal
         ::kill(::getpid(), sig);
      }

      // SIGHUP
      else if (sig == SIGHUP)
      {
         reloadConfiguration();
      }

      // Unexpected signal
      else
      {
         LOG_WARNING_MESSAGE("Unexpected signal returned from sigwait: " +
                             safe_convert::numberToString(sig));
      }
   }

   // keep compiler happy (we never get here)
   return Success();
}

} // anonymous namespace

// provide global access to handlers
namespace rstudio {
namespace server {
namespace uri_handlers {

void add(const std::string& prefix,
         const http::AsyncUriHandlerFunction& handler)
{
   s_pHttpServer->addHandler(prefix, handler);
}

void addProxyHandler(const std::string& prefix,
                     const http::AsyncUriHandlerFunction& handler)
{
   s_pHttpServer->addProxyHandler(prefix, handler);
}

void addBlocking(const std::string& prefix,
                 const http::UriHandlerFunction& handler)
{
   s_pHttpServer->addBlockingHandler(prefix, handler);
}

void setDefault(const http::AsyncUriHandlerFunction& handler)
{
   s_pHttpServer->setDefaultHandler(handler);
}

// set blocking default handler
void setBlockingDefault(const http::UriHandlerFunction& handler)
{
  s_pHttpServer->setBlockingDefaultHandler(handler);
}

void setRequestFilter(const core::http::RequestFilter& filter)
{
   s_pHttpServer->setRequestFilter(filter);
}

void setResponseFilter(const core::http::ResponseFilter& filter)
{
   s_pHttpServer->setResponseFilter(filter);
}


} // namespace uri_handlers

boost::shared_ptr<http::AsyncServer> server()
{
   return s_pHttpServer;
}

namespace scheduler {

void addCommand(boost::shared_ptr<ScheduledCommand> pCmd)
{
   s_pHttpServer->addScheduledCommand(pCmd);
}

} // namespace scheduler
} // namespace server
} // namespace rstudio


int main(int argc, char * const argv[]) 
{
   try
   {
      // initialize log
      const char * const kProgramIdentity = "rserver";
      initializeSystemLog(kProgramIdentity, core::system::kLogLevelWarning);

      // ignore SIGPIPE (don't log error because we should never call
      // syslog prior to daemonizing)
      core::system::ignoreSignal(core::system::SigPipe);

      // read program options 
      std::ostringstream osWarnings;
      Options& options = server::options();
      ProgramStatus status = options.read(argc, argv, osWarnings);
      std::string optionsWarnings = osWarnings.str();
      if ( status.exit() )
      {
         if (!optionsWarnings.empty())
            program_options::reportWarnings(optionsWarnings, ERROR_LOCATION);

         return status.exitCode() ;
      }
      
      // daemonize if requested
      if (options.serverDaemonize())
      {
         Error error = core::system::daemonize(options.serverPidFile());
         if (error)
            return core::system::exitFailure(error, ERROR_LOCATION);

         error = core::system::ignoreTerminalSignals();
         if (error)
            return core::system::exitFailure(error, ERROR_LOCATION);

         // set file creation mask to 022 (might have inherted 0 from init)
         if (options.serverSetUmask())
            setUMask(core::system::OthersNoWriteMask);
      }

      // increase the number of open files allowed (need more files
      // so we can supports lots of concurrent connectins)
      if (core::system::realUserIsRoot())
      {
         Error error = setResourceLimit(core::system::FilesLimit, 4096);
         if (error)
            return core::system::exitFailure(error, ERROR_LOCATION);
      }

      // set working directory
      Error error = FilePath(options.serverWorkingDir()).makeCurrentPath();
      if (error)
         return core::system::exitFailure(error, ERROR_LOCATION);

      // initialize crypto utils
      core::system::crypto::initialize();

      // initialize secure cookie module
      error = core::http::secure_cookie::initialize(options.secureCookieKeyFile());
      if (error)
         return core::system::exitFailure(error, ERROR_LOCATION);

      // initialize the session proxy
      error = session_proxy::initialize();
      if (error)
         return core::system::exitFailure(error, ERROR_LOCATION);

      // initialize http server
      error = httpServerInit();
      if (error)
         return core::system::exitFailure(error, ERROR_LOCATION);

      // initialize the process supervisor (needs to happen post http server
      // init for access to the scheduled command list)
      error = process_supervisor::initialize();
      if (error)
         return core::system::exitFailure(error, ERROR_LOCATION);

      // initialize monitor (needs to happen post http server init for access
      // to the server's io service)
      monitor::initializeMonitorClient(kMonitorSocketPath,
                                       server::options().monitorSharedSecret(),
                                       s_pHttpServer->ioService());

      if (!options.verifyInstallation())
      {
         // add a monitor log writer
         core::system::addLogWriter(
                   monitor::client().createLogWriter(kProgramIdentity));
      }

      // call overlay initialize
      error = overlay::initialize();
      if (error)
         return core::system::exitFailure(error, ERROR_LOCATION);

      // detect R environment variables (calls R (and this forks) so must
      // happen after daemonize so that upstart script can correctly track us
      std::string errMsg;
      bool detected = r_environment::initialize(&errMsg);
      if (!detected && overlay::requireLocalR())
      {
         program_options::reportError(errMsg, ERROR_LOCATION);
         return EXIT_FAILURE;
      }

      // add handlers and initiliaze addins (offline has distinct behavior)
      if (server::options().serverOffline())
      {
         offline::httpServerAddHandlers();
      }
      else
      {
         // add handlers
         httpServerAddHandlers();

         // initialize addins
         error = addins::initialize();
         if (error)
            return core::system::exitFailure(error, ERROR_LOCATION);

         // initialize pam auth if we don't already have an auth handler
         if (!auth::handler::isRegistered())
         {
            error = pam_auth::initialize();
            if (error)
               return core::system::exitFailure(error, ERROR_LOCATION);
         }
      }

      // give up root privilige if requested
      std::string runAsUser = options.serverUser();
      if (!runAsUser.empty())
      {
         // drop root priv
         Error error = core::system::temporarilyDropPriv(runAsUser);
         if (error)
            return core::system::exitFailure(error, ERROR_LOCATION);
      }

      // run special verify installation mode if requested
      if (options.verifyInstallation())
      {
         Error error = session_proxy::runVerifyInstallationSession();
         if (error)
            return core::system::exitFailure(error, ERROR_LOCATION);

         return EXIT_SUCCESS;
      }

      // call overlay startup
      error = overlay::startup();
      if (error)
         return core::system::exitFailure(error, ERROR_LOCATION);

      // add http server not found handler
      s_pHttpServer->setNotFoundHandler(pageNotFoundHandler);

      // run http server
      error = s_pHttpServer->run(options.wwwThreadPoolSize());
      if (error)
         return core::system::exitFailure(error, ERROR_LOCATION);

      // wait for signals
      error = waitForSignals();
      if (error)
         return core::system::exitFailure(error, ERROR_LOCATION);

      // NOTE: we never get here because waitForSignals waits forever
      return EXIT_SUCCESS;
   }
   CATCH_UNEXPECTED_EXCEPTION
   
   // if we got this far we had an unexpected exception
   return EXIT_FAILURE ;
}

