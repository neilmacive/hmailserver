// Copyright (c) 2010 Martin Knafve / hMailServer.com.  
// http://www.hmailserver.com

#include "StdAfx.h"
#include "SslContextInitializer.h"

#include "../BO/SSLCertificate.h"
#include "../Util/Encoding/Base64.h"
#include "../Util/Utilities.h"

#ifdef _DEBUG
#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

namespace HM
{
   const int CertificateAlreadyInStore = 185057381;

   bool
   SslContextInitializer::InitServer(boost::asio::ssl::context& context, std::shared_ptr<SSLCertificate> certificate, String ip_address, int port)
   {  
      if (!certificate)
      {
         String errorMessage = Formatter::Format("Error initializing SSL. Certificate not set. Address: {0}, Port: {1}", ip_address, port);
         ErrorManager::Instance()->ReportError(ErrorManager::High, 5113, "SslContextInitializer::InitServer", errorMessage);
         return false;
      }

      SetContextOptions_(context);
      EnableEllipticCurveCrypto_(context);

      if (!SetCipherList_(context))
         return false;

      try
      {         
         String bin_directory = Utilities::GetBinDirectory();
         String dh2048_file = FileUtilities::Combine(bin_directory, "dh2048.pem");

         if (FileUtilities::Exists(dh2048_file))
         {
            context.use_tmp_dh_file(AnsiString(dh2048_file));
         }
         else
         {
            ErrorManager::Instance()->ReportError(ErrorManager::Critical, 5603, "SslContextInitializer::InitServer", Formatter::Format("Unable to enable Diffie - Hellman key agreement.The required file {0} does not exist.", dh2048_file));
         }
      }
      catch (boost::system::system_error ec)
      {
         String asioError = ec.what();

         String errorMessage;
         errorMessage.Format(_T("Failed to set SSL context options. Address: %s, Port: %i, Error: %s"), 
            String(ip_address).c_str(), port, asioError.c_str());

         ErrorManager::Instance()->ReportError(ErrorManager::High, 5113, "SslContextInitializer::InitServer", errorMessage);

         return false;

      }

      AnsiString certificateFile = certificate->GetCertificateFile();
      AnsiString privateKeyFile = certificate->GetPrivateKeyFile();


      try
      {
         context.use_certificate_file(certificateFile, boost::asio::ssl::context::pem);
      }
      catch (boost::system::system_error ec)
      {
         String asioError = ec.what();

         String errorMessage;
         errorMessage.Format(_T("Failed to load certificate file. Path: %s, Address: %s, Port: %i, Error: %s"), 
            String(certificateFile).c_str(), ip_address.c_str(), port, asioError.c_str());

         ErrorManager::Instance()->ReportError(ErrorManager::High, 5113, "SslContextInitializer::InitServer", errorMessage);

         return false;
      }

      try
      {
         context.use_certificate_chain_file(certificateFile);
      }
      catch (boost::system::system_error ec)
      {
         String asioError = ec.what();

         String errorMessage;
         errorMessage.Format(_T("Failed to load certificate chain from certificate file. Path: %s, Address: %s, Port: %i, Error: %s"), 
            String(certificateFile), ip_address.c_str(), port, asioError.c_str());

         ErrorManager::Instance()->ReportError(ErrorManager::High, 5113, "SslContextInitializer::InitServer", errorMessage);

         return false;
      }

      try
      {
         context.set_password_callback(std::bind(&SslContextInitializer::GetPassword_));
         context.use_private_key_file(privateKeyFile, boost::asio::ssl::context::pem);
      }
      catch (boost::system::system_error ec)
      {
         String asioError = ec.what();

         String errorMessage;
         errorMessage.Format(_T("Failed to load private key file. Path: %s, Address: %s, Port: %i, Error: %s"), 
            String(privateKeyFile), ip_address.c_str(), port, asioError.c_str());

         ErrorManager::Instance()->ReportError(ErrorManager::High, 5113, "SslContextInitializer::InitServer", errorMessage);

         return false;
      }
      catch (...)
      {
         String errorMessage = "Error initializing SSL";
         ErrorManager::Instance()->ReportError(ErrorManager::High, 5113, "SslContextInitializer::InitServer", errorMessage);
         return false;
      }


      return true;
   }

   bool
   SslContextInitializer::InitClient(boost::asio::ssl::context& context)
   { 
      SetContextOptions_(context);
         
      if (!SetCipherList_(context))
         return false;

      return true;
   }

   std::string 
   SslContextInitializer::GetPassword_()
   {
      ErrorManager::Instance()->ReportError(ErrorManager::High, 5143, "TCPServer::GetPassword()", "The private key file has a password. hMailServer does not support this.");
      return "";
   }

   bool
   SslContextInitializer::SetCipherList_(boost::asio::ssl::context& context)
   {
      AnsiString cipher_list = Configuration::Instance()->GetSslCipherList();

      cipher_list.Replace("\r", "");
      cipher_list.Replace("\n", "");
      cipher_list.Replace(" ", "");

      if (cipher_list.Trim().IsEmpty())
         return true;

      // Asio does not expose cipher list. Access underlaying layer (OpenSSL) directly.
      SSL_CTX* ssl = context.native_handle();
      int result = SSL_CTX_set_cipher_list(ssl, cipher_list.c_str());

      if (result == 0)
      {
         ErrorManager::Instance()->ReportError(ErrorManager::Medium, 5511,"SslContextInitializer::SetCipherList_", "Failed to set SSL ciphers");
         return false;
      }

      return true;
   }

   void
   SslContextInitializer::SetContextOptions_(boost::asio::ssl::context& context)
   {
      bool sslv30 = Configuration::Instance()->GetSslVersionEnabled(SslVersion30);
      bool tlsv10 = Configuration::Instance()->GetSslVersionEnabled(TlsVersion10);
      bool tlsv11 = Configuration::Instance()->GetSslVersionEnabled(TlsVersion11);
      bool tlsv12 = Configuration::Instance()->GetSslVersionEnabled(TlsVersion12);

      int options = SSL_OP_ALL | SSL_OP_SINGLE_DH_USE | SSL_OP_NO_SSLv2 | SSL_OP_SINGLE_ECDH_USE;

      if (!sslv30)
         options = options | SSL_OP_NO_SSLv3;
      if (!tlsv10)
         options = options | SSL_OP_NO_TLSv1;
      if (!tlsv11)
         options = options | SSL_OP_NO_TLSv1_1;
      if (!tlsv12)
         options = options | SSL_OP_NO_TLSv1_2;

      SSL_CTX* ssl = context.native_handle();
      SSL_CTX_set_options(ssl, options);
   }

   void
   SslContextInitializer::EnableEllipticCurveCrypto_(boost::asio::ssl::context& context)
   {
      SSL_CTX* ssl = context.native_handle();

      EC_KEY *ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
      if (!ecdh)
      {
         ErrorManager::Instance()->ReportError(ErrorManager::Medium, 5511, "SslContextInitializer::SetCipherList_", "Failed to enable TLS EC");
         return;
      }

      int set_tmp_ecdhResult = SSL_CTX_set_tmp_ecdh(ssl, ecdh);
      if (set_tmp_ecdhResult != 1)
      {
         ErrorManager::Instance()->ReportError(ErrorManager::Medium, 5511, "SslContextInitializer::SetCipherList_", Formatter::Format("Failed to enable TLS EC. SSL_CTX_set_tmp_ecdh returend {0}", set_tmp_ecdhResult));
      }
      
      EC_KEY_free(ecdh);
   }

}