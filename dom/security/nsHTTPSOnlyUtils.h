/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHTTPSOnlyUtils_h___
#define nsHTTPSOnlyUtils_h___

#include "nsIScriptError.h"
#include "nsISupports.h"
#include "mozilla/net/DocumentLoadListener.h"

class nsHTTPSOnlyUtils {
 public:
  /**
   * Returns if HTTPSOnly-Mode preference is enabled
   * @param aFromPrivateWindow true if executing in private browsing mode
   * @return true if HTTPS-Only Mode is enabled
   */
  static bool IsHttpsOnlyModeEnabled(bool aFromPrivateWindow);

  /**
   * Potentially fires an http request for a top-level load (provided by
   * aDocumentLoadListener) in the background to avoid long timeouts in case
   * the upgraded https top-level load most likely will result in timeout.
   * @param aDocumentLoadListener The Document listener associated with
   *                              the original top-level channel.
   */
  static void PotentiallyFireHttpRequestToShortenTimout(
      mozilla::net::DocumentLoadListener* aDocumentLoadListener);

  /**
   * Determines if a request should get upgraded because of the HTTPS-Only mode.
   * If true, the httpsOnlyStatus flag in LoadInfo gets updated and a message is
   * logged in the console.
   * @param  aURI      nsIURI of request
   * @param  aLoadInfo nsILoadInfo of request
   * @return           true if request should get upgraded
   */
  static bool ShouldUpgradeRequest(nsIURI* aURI, nsILoadInfo* aLoadInfo);

  /**
   * Determines if a request should get upgraded because of the HTTPS-Only mode.
   * If true, a message is logged in the console.
   * @param  aURI      nsIURI of request
   * @param  aLoadInfo nsILoadInfo of request
   * @return           true if request should get upgraded
   */
  static bool ShouldUpgradeWebSocket(nsIURI* aURI, nsILoadInfo* aLoadInfo);

  /**
   * Checks if the error code is on a block-list of codes that are probably not
   * related to a HTTPS-Only Mode upgrade.
   * @param  aChannel The failed Channel.
   * @param  aError Error Code from Request
   * @return        false if error is not related to upgrade
   */
  static bool CouldBeHttpsOnlyError(nsIChannel* aChannel, nsresult aError);

  /**
   * Logs localized message to either content console or browser console
   * @param aName      Localization key
   * @param aParams    Localization parameters
   * @param aFlags     Logging Flag (see nsIScriptError)
   * @param aLoadInfo  The loadinfo of the request.
   * @param [aURI]     Optional: URI to log
   */
  static void LogLocalizedString(const char* aName,
                                 const nsTArray<nsString>& aParams,
                                 uint32_t aFlags, nsILoadInfo* aLoadInfo,
                                 nsIURI* aURI = nullptr);

  /**
   * Tests if the HTTPS-Only Mode upgrade exception is set for a given channel.
   * Note: This function only adds an exemption for loads of TYPE_DOCUMENT.
   * @param  aChannel The channel to be checked
   */
  static void TestSitePermissionAndPotentiallyAddExemption(
      nsIChannel* aChannel);

  /**
   * Checks whether CORS or mixed content requests are safe because they'll get
   * upgraded to HTTPS
   * @param  aLoadInfo nsILoadInfo of request
   * @return           true if it's safe to accept
   */
  static bool IsSafeToAcceptCORSOrMixedContent(nsILoadInfo* aLoadInfo);

 private:
  /**
   * Logs localized message to either content console or browser console
   * @param aMessage   Message to log
   * @param aFlags     Logging Flag (see nsIScriptError)
   * @param aLoadInfo  The loadinfo of the request.
   * @param [aURI]     Optional: URI to log
   */
  static void LogMessage(const nsAString& aMessage, uint32_t aFlags,
                         nsILoadInfo* aLoadInfo, nsIURI* aURI = nullptr);

  /**
   * Checks whether the URI ends with .onion
   * @param  aURI URI object
   * @return      true if the URI is an Onion URI
   */
  static bool OnionException(nsIURI* aURI);

  /**
   * Checks whether the URI is a loopback- or local-IP
   * @param  aURI URI object
   * @return      true if the URI is either loopback or local
   */
  static bool LoopbackOrLocalException(nsIURI* aURI);
};

/**
 * Helper class to perform an http request with a N milliseconds
 * delay. If that http request is 'receiving data' before the
 * upgraded https request, then it's a strong indicator that
 * the https request will result in a timeout and hence we
 * cancel the https request which will result in displaying
 * the exception page.
 */
class TestHTTPAnswerRunnable final : public mozilla::Runnable,
                                     public nsIStreamListener,
                                     public nsIInterfaceRequestor,
                                     public nsITimerCallback {
 public:
  // TestHTTPAnswerRunnable needs to implement all these channel related
  // interfaces because otherwise our Necko code is not happy, but we
  // really only care about ::OnStartRequest.
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIRUNNABLE
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSITIMERCALLBACK

  explicit TestHTTPAnswerRunnable(
      nsIURI* aURI, mozilla::net::DocumentLoadListener* aDocumentLoadListener);

 protected:
  ~TestHTTPAnswerRunnable() = default;

 private:
  RefPtr<nsIURI> mURI;
  RefPtr<mozilla::net::DocumentLoadListener> mDocumentLoadListener;
  RefPtr<nsITimer> mTimer;
};

#endif /* nsHTTPSOnlyUtils_h___ */
