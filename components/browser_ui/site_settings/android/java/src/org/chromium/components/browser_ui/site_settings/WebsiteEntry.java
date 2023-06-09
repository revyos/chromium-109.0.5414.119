// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.browser_ui.site_settings;

import org.chromium.url.GURL;

import java.io.Serializable;

/**
 * Used by {@link WebsiteRowPreference} to display various information about one or multiple sites.
 */
public interface WebsiteEntry extends Serializable {
    /** @return the title to display in a {@link WebsiteRowPreference}. */
    String getTitleForPreferenceRow();

    /** @return the URL for fetching a favicon. */
    GURL getFaviconUrl();

    /** @return the total bytes used for associated storage. */
    long getTotalUsage();

    /** @return the total number of cookies associated with the entry. */
    int getNumberOfCookies();

    /**
     * @return whether either the eTLD+1 or one of the origins associated with it matches the given
     * search query.
     */
    boolean matches(String search);
}
