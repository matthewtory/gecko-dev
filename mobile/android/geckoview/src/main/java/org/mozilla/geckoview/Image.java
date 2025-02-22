/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * vim: ts=4 sw=4 expandtab:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.graphics.Bitmap;
import android.support.annotation.AnyThread;
import android.support.annotation.NonNull;

import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ImageResource;

/**
 * Represents an Web API image resource as used in web app manifests and media
 * session metadata.
 */
@AnyThread
public class Image {
    private final ImageResource.Collection mCollection;

    /* package */ Image(final ImageResource.Collection collection) {
        mCollection = collection;
    }

    /* package */ static Image fromSizeSrcBundle(final GeckoBundle bundle) {
        return new Image(ImageResource.Collection.fromSizeSrcBundle(bundle));
    }

    /**
     * Get the best version of this image for size <code>size</code>.
     * Embedders are encouraged to cache the result of this method keyed
     * with this instance.
     *
     * @param size pixel size at which this image will be displayed at.
     *
     * @return A {@link GeckoResult} that resolves to the bitmap when ready.
     */
    @NonNull
    public GeckoResult<Bitmap> getBitmap(final int size) {
        return mCollection.getBitmap(size);
    }
}
