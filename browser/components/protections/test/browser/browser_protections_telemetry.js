/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

XPCOMUtils.defineLazyServiceGetter(
  this,
  "TrackingDBService",
  "@mozilla.org/tracking-db-service;1",
  "nsITrackingDBService"
);

const { AboutProtectionsParent } = ChromeUtils.import(
  "resource:///actors/AboutProtectionsParent.jsm"
);

const LOG = {
  "https://1.example.com": [
    [Ci.nsIWebProgressListener.STATE_BLOCKED_TRACKING_CONTENT, true, 1],
  ],
  "https://2.example.com": [
    [Ci.nsIWebProgressListener.STATE_BLOCKED_FINGERPRINTING_CONTENT, true, 1],
  ],
  "https://3.example.com": [
    [Ci.nsIWebProgressListener.STATE_BLOCKED_CRYPTOMINING_CONTENT, true, 2],
  ],
  "https://4.example.com": [
    [Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_TRACKER, true, 3],
  ],
  "https://5.example.com": [
    [Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_TRACKER, true, 1],
  ],
  // Cookie blocked for other reason, then identified as a tracker
  "https://6.example.com": [
    [
      Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_ALL |
        Ci.nsIWebProgressListener.STATE_LOADED_LEVEL_1_TRACKING_CONTENT,
      true,
      4,
    ],
  ],
};

requestLongerTimeout(2);

add_task(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.contentblocking.database.enabled", true],
      ["browser.contentblocking.report.monitor.enabled", true],
      ["browser.contentblocking.report.lockwise.enabled", true],
      ["browser.contentblocking.report.proxy.enabled", true],
      // Change the endpoints to prevent non-local network connections when landing on the page.
      ["browser.contentblocking.report.monitor.url", ""],
      ["browser.contentblocking.report.monitor.sign_in_url", ""],
      ["browser.contentblocking.report.social.url", ""],
      ["browser.contentblocking.report.cookie.url", ""],
      ["browser.contentblocking.report.tracker.url", ""],
      ["browser.contentblocking.report.fingerprinter.url", ""],
      ["browser.contentblocking.report.cryptominer.url", ""],
      ["browser.contentblocking.report.lockwise.mobile-android.url", ""],
      ["browser.contentblocking.report.lockwise.mobile-ios.url", ""],
      ["browser.contentblocking.report.mobile-ios.url", ""],
      ["browser.contentblocking.report.mobile-android.url", ""],
      ["browser.contentblocking.report.monitor.home_page_url", ""],
      ["browser.contentblocking.report.monitor.preferences_url", ""],
    ],
  });

  let oldCanRecord = Services.telemetry.canRecordExtended;
  Services.telemetry.canRecordExtended = true;
  registerCleanupFunction(() => {
    Services.telemetry.canRecordExtended = oldCanRecord;
  });
});

add_task(async function checkTelemetryLoadEvents() {
  // There's an arbitrary interval of 2 seconds in which the content
  // processes sync their event data with the parent process, we wait
  // this out to ensure that we clear everything that is left over from
  // previous tests and don't receive random events in the middle of our tests.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(c => setTimeout(c, 2000));

  // Clear everything.
  Services.telemetry.clearEvents();
  await TestUtils.waitForCondition(() => {
    let events = Services.telemetry.snapshotEvents(
      Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
      true
    ).content;
    return !events || !events.length;
  });

  Services.telemetry.setEventRecordingEnabled("security.ui.protections", true);

  let tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:protections",
    gBrowser,
  });

  let loadEvents = await TestUtils.waitForCondition(() => {
    let events = Services.telemetry.snapshotEvents(
      Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
      true
    ).content;
    if (events && events.length) {
      events = events.filter(
        e => e[1] == "security.ui.protections" && e[2] == "show"
      );
      if (events.length == 1) {
        return events;
      }
    }
    return null;
  }, "recorded telemetry for showing the report");

  is(loadEvents.length, 1, `recorded telemetry for showing the report`);
  await reloadTab(tab);
  loadEvents = await TestUtils.waitForCondition(() => {
    let events = Services.telemetry.snapshotEvents(
      Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
      true
    ).content;
    if (events && events.length) {
      events = events.filter(
        e => e[1] == "security.ui.protections" && e[2] == "close"
      );
      if (events.length == 1) {
        return events;
      }
    }
    return null;
  }, "recorded telemetry for closing the report");

  is(loadEvents.length, 1, `recorded telemetry for closing the report`);

  await BrowserTestUtils.removeTab(tab);
});

function waitForTelemetryEventCount(count) {
  info("waiting for telemetry event count of " + count);
  return TestUtils.waitForCondition(() => {
    let events = Services.telemetry.snapshotEvents(
      Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
      false
    ).content;
    if (!events) {
      return null;
    }
    // Ignore irrelevant events from other parts of the browser.
    events = events.filter(e => e[1] == "security.ui.protections");
    info("got " + (events && events.length) + " events");
    if (events.length == count) {
      return events;
    }
    return null;
  }, "waiting for telemetry event count of: " + count);
}

add_task(async function checkTelemetryClickEvents() {
  // There's an arbitrary interval of 2 seconds in which the content
  // processes sync their event data with the parent process, we wait
  // this out to ensure that we clear everything that is left over from
  // previous tests and don't receive random events in the middle of our tests.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(c => setTimeout(c, 2000));

  // Clear everything.
  Services.telemetry.clearEvents();
  await TestUtils.waitForCondition(() => {
    let events = Services.telemetry.snapshotEvents(
      Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
      true
    ).content;
    return !events || !events.length;
  });

  Services.telemetry.setEventRecordingEnabled("security.ui.protections", true);

  let tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:protections",
    gBrowser,
  });

  // Add user logins.
  Services.logins.addLogin(TEST_LOGIN1);
  await reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const managePasswordsButton = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("manage-passwords-button");
      },
      "Manage passwords button exists"
    );
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.is_visible(managePasswordsButton),
      "manage passwords button is visible"
    );
    managePasswordsButton.click();
  });

  let events = await waitForTelemetryEventCount(4);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "lw_open_button" &&
      e[4] == "manage_passwords"
  );
  is(
    events.length,
    1,
    `recorded telemetry for lw_open_button when there are no breached passwords`
  );
  await BrowserTestUtils.removeTab(gBrowser.selectedTab);

  // Add breached logins.
  AboutProtectionsParent.setTestOverride(
    mockGetLoginDataWithSyncedDevices(false, 4)
  );
  await reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const managePasswordsButton = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("manage-passwords-button");
      },
      "Manage passwords button exists"
    );
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.is_visible(managePasswordsButton),
      "manage passwords button is visible"
    );
    managePasswordsButton.click();
  });

  events = await waitForTelemetryEventCount(7);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "lw_open_button" &&
      e[4] == "manage_breached_passwords"
  );
  is(
    events.length,
    1,
    `recorded telemetry for lw_open_button when there are breached passwords`
  );
  AboutProtectionsParent.setTestOverride(null);
  Services.logins.removeLogin(TEST_LOGIN1);
  await BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await reloadTab(tab);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    // Show all elements, so we can click on them, even though our user is not logged in.
    let hidden_elements = content.document.querySelectorAll(".hidden");
    for (let el of hidden_elements) {
      el.style.display = "block ";
    }

    const savePasswordsButton = await ContentTaskUtils.waitForCondition(() => {
      // Opens an extra tab
      return content.document.getElementById("save-passwords-button");
    }, "Save Passwords button exists");

    savePasswordsButton.click();
  });

  events = await waitForTelemetryEventCount(10);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "lw_open_button" &&
      e[4] == "save_passwords"
  );
  is(
    events.length,
    1,
    `recorded telemetry for lw_open_button when there are no stored passwords`
  );
  await BrowserTestUtils.removeTab(gBrowser.selectedTab);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const lockwiseAndroidAppLink = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("lockwise-android-inline-link");
      },
      "lockwiseAndroidAppLink exists"
    );

    lockwiseAndroidAppLink.click();
  });

  events = await waitForTelemetryEventCount(11);

  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "lw_sync_link" &&
      e[4] == "android"
  );
  is(events.length, 1, `recorded telemetry for lw_sync_link, android`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const lockwiseAboutLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("lockwise-how-it-works");
    }, "lockwiseReportLink exists");

    lockwiseAboutLink.click();
  });

  events = await waitForTelemetryEventCount(12);

  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "lw_about_link"
  );
  is(events.length, 1, `recorded telemetry for lw_about_link`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    let monitorAboutLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("monitor-link");
    }, "monitorAboutLink exists");

    monitorAboutLink.click();
  });

  events = await waitForTelemetryEventCount(13);

  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "mtr_about_link"
  );
  is(events.length, 1, `recorded telemetry for mtr_about_link`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const signUpForMonitorLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("sign-up-for-monitor-link");
    }, "signUpForMonitorLink exists");

    signUpForMonitorLink.click();
  });

  events = await waitForTelemetryEventCount(14);

  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "mtr_signup_button"
  );
  is(events.length, 1, `recorded telemetry for mtr_signup_button`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const socialLearnMoreLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("social-link");
    }, "Learn more link for social tab exists");

    socialLearnMoreLink.click();
  });

  events = await waitForTelemetryEventCount(15);

  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "trackers_about_link" &&
      e[4] == "social"
  );
  is(events.length, 1, `recorded telemetry for social trackers_about_link`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const cookieLearnMoreLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("cookie-link");
    }, "Learn more link for cookie tab exists");

    cookieLearnMoreLink.click();
  });

  events = await waitForTelemetryEventCount(16);

  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "trackers_about_link" &&
      e[4] == "cookie"
  );
  is(events.length, 1, `recorded telemetry for cookie trackers_about_link`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const trackerLearnMoreLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("tracker-link");
    }, "Learn more link for tracker tab exists");

    trackerLearnMoreLink.click();
  });

  events = await waitForTelemetryEventCount(17);

  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "trackers_about_link" &&
      e[4] == "tracker"
  );
  is(
    events.length,
    1,
    `recorded telemetry for content tracker trackers_about_link`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const fingerprinterLearnMoreLink = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("fingerprinter-link");
      },
      "Learn more link for fingerprinter tab exists"
    );

    fingerprinterLearnMoreLink.click();
  });

  events = await waitForTelemetryEventCount(18);

  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "trackers_about_link" &&
      e[4] == "fingerprinter"
  );
  is(
    events.length,
    1,
    `recorded telemetry for fingerprinter trackers_about_link`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const cryptominerLearnMoreLink = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("cryptominer-link");
      },
      "Learn more link for cryptominer tab exists"
    );

    cryptominerLearnMoreLink.click();
  });

  events = await waitForTelemetryEventCount(19);

  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "trackers_about_link" &&
      e[4] == "cryptominer"
  );
  is(
    events.length,
    1,
    `recorded telemetry for cryptominer trackers_about_link`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const lockwiseIOSAppLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("lockwise-ios-inline-link");
    }, "lockwiseIOSAppLink exists");

    lockwiseIOSAppLink.click();
  });

  events = await waitForTelemetryEventCount(20);

  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "lw_sync_link" &&
      e[4] == "ios"
  );
  is(events.length, 1, `recorded telemetry for lw_sync_link`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const mobileAppLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("android-mobile-inline-link");
    }, "android-mobile-inline-link exists");

    mobileAppLink.click();
  });

  events = await waitForTelemetryEventCount(21);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "mobile_app_link"
  );
  is(events.length, 1, `recorded telemetry for mobile_app_link`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const protectionSettings = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("protection-settings");
    }, "protection-settings link exists");

    protectionSettings.click();
  });

  events = await waitForTelemetryEventCount(22);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "settings_link" &&
      e[4] == "header-settings"
  );
  is(events.length, 1, `recorded telemetry for settings_link header-settings`);
  await BrowserTestUtils.removeTab(gBrowser.selectedTab);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const customProtectionSettings = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("manage-protections");
      },
      "manage-protections link exists"
    );
    // Show element so we can click on it
    customProtectionSettings.style.display = "block";

    customProtectionSettings.click();
  });

  events = await waitForTelemetryEventCount(23);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "settings_link" &&
      e[4] == "custom-card-settings"
  );
  is(
    events.length,
    1,
    `recorded telemetry for settings_link custom-card-settings`
  );
  await BrowserTestUtils.removeTab(gBrowser.selectedTab);

  // Add breached logins and some resolved breaches.
  AboutProtectionsParent.setTestOverride(
    mockGetMonitorData({
      potentiallyBreachedLogins: 4,
      numBreaches: 3,
      numBreachesResolved: 1,
    })
  );
  await reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const resolveBreachesButton = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("monitor-partial-breaches-link");
      },
      "Monitor resolve breaches button exists"
    );

    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.is_visible(resolveBreachesButton),
      "Resolve breaches button is visible"
    );

    resolveBreachesButton.click();
  });

  events = await waitForTelemetryEventCount(26);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "mtr_report_link" &&
      e[4] == "resolve_breaches"
  );
  is(events.length, 1, `recorded telemetry for resolve breaches button`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const monitorKnownBreachesBlock = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("monitor-known-breaches-link");
      },
      "Monitor card known breaches block exists"
    );

    monitorKnownBreachesBlock.click();
  });

  events = await waitForTelemetryEventCount(27);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "mtr_report_link" &&
      e[4] == "known_resolved_breaches"
  );
  is(events.length, 1, `recorded telemetry for monitor known breaches block`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const monitorExposedPasswordsBlock = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById(
          "monitor-exposed-passwords-link"
        );
      },
      "Monitor card exposed passwords block exists"
    );

    monitorExposedPasswordsBlock.click();
  });

  events = await waitForTelemetryEventCount(28);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "mtr_report_link" &&
      e[4] == "exposed_passwords_unresolved_breaches"
  );
  is(
    events.length,
    1,
    `recorded telemetry for monitor exposed passwords block`
  );

  // Add breached logins and no resolved breaches.
  AboutProtectionsParent.setTestOverride(
    mockGetMonitorData({
      potentiallyBreachedLogins: 4,
      numBreaches: 3,
      numBreachesResolved: 0,
    })
  );
  await reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const manageBreachesButton = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("monitor-breaches-link");
    }, "Monitor manage breaches button exists");

    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.is_visible(manageBreachesButton),
      "Manage breaches button is visible"
    );

    manageBreachesButton.click();
  });

  events = await waitForTelemetryEventCount(31);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "mtr_report_link" &&
      e[4] == "manage_breaches"
  );
  is(events.length, 1, `recorded telemetry for manage breaches button`);

  // All breaches are resolved.
  AboutProtectionsParent.setTestOverride(
    mockGetMonitorData({
      potentiallyBreachedLogins: 4,
      numBreaches: 3,
      numBreachesResolved: 3,
    })
  );
  await reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const viewReportButton = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("monitor-breaches-link");
    }, "Monitor view report button exists");
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.is_visible(viewReportButton),
      "View report button is visible"
    );

    viewReportButton.click();
  });

  events = await waitForTelemetryEventCount(34);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "mtr_report_link" &&
      e[4] == "view_report"
  );
  is(events.length, 1, `recorded telemetry for view report button`);

  // No breaches are present.
  AboutProtectionsParent.setTestOverride(
    mockGetMonitorData({
      potentiallyBreachedLogins: 4,
      numBreaches: 0,
      numBreachesResolved: 0,
    })
  );
  await reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const viewReportButton = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("monitor-breaches-link");
    }, "Monitor view report button exists");
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.is_visible(viewReportButton),
      "View report button is visible"
    );

    viewReportButton.click();
  });

  events = await waitForTelemetryEventCount(37);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "mtr_report_link" &&
      e[4] == "view_report"
  );
  is(events.length, 2, `recorded telemetry for view report button`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const monitorEmailBlock = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("monitor-stored-emails-link");
    }, "Monitor card email block exists");

    monitorEmailBlock.click();
  });

  events = await waitForTelemetryEventCount(38);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "mtr_report_link" &&
      e[4] == "stored_emails"
  );
  is(events.length, 1, `recorded telemetry for monitor email block`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const monitorKnownBreachesBlock = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("monitor-known-breaches-link");
      },
      "Monitor card known breaches block exists"
    );

    monitorKnownBreachesBlock.click();
  });

  events = await waitForTelemetryEventCount(39);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "mtr_report_link" &&
      e[4] == "known_unresolved_breaches"
  );
  is(events.length, 1, `recorded telemetry for monitor known breaches block`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function() {
    const monitorExposedPasswordsBlock = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById(
          "monitor-exposed-passwords-link"
        );
      },
      "Monitor card exposed passwords block exists"
    );

    monitorExposedPasswordsBlock.click();
  });

  events = await waitForTelemetryEventCount(40);
  events = events.filter(
    e =>
      e[1] == "security.ui.protections" &&
      e[2] == "click" &&
      e[3] == "mtr_report_link" &&
      e[4] == "exposed_passwords_all_breaches"
  );
  is(
    events.length,
    1,
    `recorded telemetry for monitor exposed passwords block`
  );

  // Clean up.
  AboutProtectionsParent.setTestOverride(null);
  await BrowserTestUtils.removeTab(tab);
});

// This tests that telemetry is sent when saveEvents is called.
add_task(async function test_save_telemetry() {
  // Clear all scalar telemetry.
  Services.telemetry.clearScalars();

  await TrackingDBService.saveEvents(JSON.stringify(LOG));

  const scalars = Services.telemetry.getSnapshotForScalars("main", false)
    .parent;
  is(scalars["contentblocking.trackers_blocked_count"], 6);

  // Use the TrackingDBService API to delete the data.
  await TrackingDBService.clearAll();
});

// Test that telemetry is sent if entrypoint param is included,
// and test that it is recorded as default if entrypoint param is not properly included
add_task(async function checkTelemetryLoadEventForEntrypoint() {
  // There's an arbitrary interval of 2 seconds in which the content
  // processes sync their event data with the parent process, we wait
  // this out to ensure that we clear everything that is left over from
  // previous tests and don't receive random events in the middle of our tests.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(c => setTimeout(c, 2000));

  // Clear everything.
  Services.telemetry.clearEvents();
  await TestUtils.waitForCondition(() => {
    let events = Services.telemetry.snapshotEvents(
      Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
      true
    ).content;
    return !events || !events.length;
  });

  Services.telemetry.setEventRecordingEnabled("security.ui.protections", true);

  info("Typo in 'entrypoint' should not be recorded");
  let tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:protections?entryPoint=newPage",
    gBrowser,
  });

  let loadEvents = await TestUtils.waitForCondition(() => {
    let events = Services.telemetry.snapshotEvents(
      Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
      true
    ).content;
    if (events && events.length) {
      events = events.filter(
        e =>
          e[1] == "security.ui.protections" &&
          e[2] == "show" &&
          e[4] == "direct"
      );
      if (events.length == 1) {
        return events;
      }
    }
    return null;
  }, "recorded telemetry for showing the report contains default 'direct' entrypoint");

  is(
    loadEvents.length,
    1,
    `recorded telemetry for showing the report contains default 'direct' entrypoint`
  );

  await BrowserTestUtils.removeTab(tab);

  tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:protections?entrypoint=page",
    gBrowser,
  });

  loadEvents = await TestUtils.waitForCondition(() => {
    let events = Services.telemetry.snapshotEvents(
      Ci.nsITelemetry.DATASET_PRERELEASE_CHANNELS,
      true
    ).content;
    if (events && events.length) {
      events = events.filter(
        e =>
          e[1] == "security.ui.protections" && e[2] == "show" && e[4] == "page"
      );
      if (events.length == 1) {
        return events;
      }
    }
    return null;
  }, "recorded telemetry for showing the report contains correct entrypoint");

  is(
    loadEvents.length,
    1,
    "recorded telemetry for showing the report contains correct entrypoint"
  );

  // Clean up.
  await BrowserTestUtils.removeTab(tab);
});
