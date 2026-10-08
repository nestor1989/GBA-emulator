package com.nestordelrio.emu

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import com.android.billingclient.api.AcknowledgePurchaseParams
import com.android.billingclient.api.BillingClient
import com.android.billingclient.api.BillingClientStateListener
import com.android.billingclient.api.BillingFlowParams
import com.android.billingclient.api.BillingResult
import com.android.billingclient.api.ConsumeParams
import com.android.billingclient.api.PendingPurchasesParams
import com.android.billingclient.api.ProductDetails
import com.android.billingclient.api.Purchase
import com.android.billingclient.api.PurchasesUpdatedListener
import com.android.billingclient.api.QueryProductDetailsParams
import com.android.billingclient.api.QueryPurchasesParams

/**
 * Supporter pack and tips through Google Play Billing.
 *
 * Everything that plays games is free. The pack (non-consumable) only unlocks
 * cosmetics: control themes, app icons and a badge. Tips (consumable) unlock
 * nothing. Purchases are checked on the device only: there is no server, and
 * the worst a tampered client gets is a free color scheme.
 *
 * All public calls and listener callbacks happen on the main thread.
 */
object Support {

    const val PACK = "supporter_pack"
    const val TIP_SMALL = "tip_small"
    const val TIP_MEDIUM = "tip_medium"
    const val TIP_LARGE = "tip_large"
    val TIPS = listOf(TIP_SMALL, TIP_MEDIUM, TIP_LARGE)
    private val PRODUCTS = listOf(PACK) + TIPS

    enum class Store { CONNECTING, READY, UNAVAILABLE }

    enum class Event { THANKS_PACK, THANKS_TIP, PENDING, ERROR }

    interface Listener {
        /** Store state, prices, ownership or pending state changed. */
        fun onSupportChanged()

        fun onSupportEvent(event: Event)
    }

    private const val PREFS = "support"
    private const val KEY_OWNED = "pack_owned"
    private const val KEY_DEBUG_UNLOCK = "debug_unlock"

    private val main = Handler(Looper.getMainLooper())
    private val listeners = LinkedHashSet<Listener>()
    private var appContext: Context? = null
    private var client: BillingClient? = null
    private val details = HashMap<String, ProductDetails>()

    var store = Store.CONNECTING
        private set

    /** The pack was bought but Play is still waiting for the payment (cash, bank transfer...). */
    var packPending = false
        private set

    /** Product details have been fetched at least once (prices are known or definitely missing). */
    var productsLoaded = false
        private set

    /** Starts the connection once per process. Cheap to call from every screen. */
    fun init(context: Context) {
        if (client != null) return
        val app = context.applicationContext
        appContext = app
        client = BillingClient.newBuilder(app)
            .setListener(PurchasesUpdatedListener { result, purchases ->
                main.post { onPurchasesUpdated(result, purchases) }
            })
            .enablePendingPurchases(PendingPurchasesParams.newBuilder().enableOneTimeProducts().build())
            .enableAutoServiceReconnection()
            .build()
        connect()
    }

    fun addListener(listener: Listener) {
        listeners += listener
    }

    fun removeListener(listener: Listener) {
        listeners -= listener
    }

    /** True when the pack is owned. Answers from a local cache, so it works offline and before Play replies. */
    fun isSupporter(context: Context): Boolean {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        return prefs.getBoolean(KEY_OWNED, false) ||
            (BuildConfig.DEBUG && prefs.getBoolean(KEY_DEBUG_UNLOCK, false))
    }

    /** Debug builds only: unlocks the cosmetics without paying, to QA them before the Play products exist. */
    fun setDebugUnlocked(context: Context, unlocked: Boolean) {
        if (!BuildConfig.DEBUG) return
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(KEY_DEBUG_UNLOCK, unlocked).apply()
        if (!isSupporter(context)) Cosmetics.resetToFree(context)
        notifyChanged()
    }

    fun isDebugUnlocked(context: Context): Boolean =
        BuildConfig.DEBUG &&
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(KEY_DEBUG_UNLOCK, false)

    /** Localized price ("US$2.99"), or null while loading or when the product is not set up in Play Console. */
    fun price(productId: String): String? = details[productId]?.let { offer(it)?.formattedPrice }

    /**
     * Re-reads the purchases Play knows for this account: restores the pack
     * after a reinstall, finishes purchases paid while the app was closed and
     * notices refunds. [onDone] gets whether the pack is owned, or null when
     * Play could not be reached.
     */
    fun refresh(onDone: ((Boolean?) -> Unit)? = null) {
        val billing = client
        if (billing == null || store != Store.READY) {
            // Setup runs a refresh of its own when it finishes; retry a failed setup.
            if (store == Store.UNAVAILABLE) connect()
            onDone?.invoke(null)
            return
        }
        val params = QueryPurchasesParams.newBuilder().setProductType(BillingClient.ProductType.INAPP).build()
        billing.queryPurchasesAsync(params) { result, purchases ->
            main.post {
                if (result.responseCode == BillingClient.BillingResponseCode.OK) {
                    handlePurchases(purchases, fromUser = false, complete = true)
                    onDone?.invoke(appContext?.let { isSupporter(it) } ?: false)
                } else {
                    onDone?.invoke(null)
                }
            }
        }
    }

    /** Opens Google Play's purchase sheet. Returns false when it could not be shown. */
    fun buy(activity: Activity, productId: String): Boolean {
        val billing = client ?: return false
        val product = details[productId] ?: return false
        val productParams = BillingFlowParams.ProductDetailsParams.newBuilder().setProductDetails(product)
        offer(product)?.offerToken?.let { productParams.setOfferToken(it) }
        val flow = BillingFlowParams.newBuilder().setProductDetailsParamsList(listOf(productParams.build())).build()
        val result = billing.launchBillingFlow(activity, flow)
        return when (result.responseCode) {
            BillingClient.BillingResponseCode.OK -> true
            BillingClient.BillingResponseCode.ITEM_ALREADY_OWNED -> {
                refresh()
                true
            }
            else -> false
        }
    }

    // ---- Internals ----------------------------------------------------------

    private fun offer(product: ProductDetails): ProductDetails.OneTimePurchaseOfferDetails? =
        product.oneTimePurchaseOfferDetailsList?.firstOrNull() ?: product.oneTimePurchaseOfferDetails

    private fun connect() {
        val billing = client ?: return
        store = Store.CONNECTING
        notifyChanged()
        billing.startConnection(object : BillingClientStateListener {
            override fun onBillingSetupFinished(result: BillingResult) {
                main.post {
                    if (result.responseCode == BillingClient.BillingResponseCode.OK) {
                        store = Store.READY
                        queryProducts()
                        refresh()
                    } else {
                        store = Store.UNAVAILABLE
                    }
                    notifyChanged()
                }
            }

            override fun onBillingServiceDisconnected() {
                // enableAutoServiceReconnection() reconnects on the next call.
            }
        })
    }

    private fun queryProducts() {
        val billing = client ?: return
        val products = PRODUCTS.map {
            QueryProductDetailsParams.Product.newBuilder()
                .setProductId(it)
                .setProductType(BillingClient.ProductType.INAPP)
                .build()
        }
        val params = QueryProductDetailsParams.newBuilder().setProductList(products).build()
        billing.queryProductDetailsAsync(params) { result, response ->
            main.post {
                if (result.responseCode == BillingClient.BillingResponseCode.OK) {
                    details.clear()
                    for (product in response.productDetailsList) details[product.productId] = product
                }
                productsLoaded = true
                notifyChanged()
            }
        }
    }

    private fun onPurchasesUpdated(result: BillingResult, purchases: List<Purchase>?) {
        when (result.responseCode) {
            BillingClient.BillingResponseCode.OK ->
                handlePurchases(purchases.orEmpty(), fromUser = true, complete = false)
            BillingClient.BillingResponseCode.USER_CANCELED -> Unit
            BillingClient.BillingResponseCode.ITEM_ALREADY_OWNED -> refresh()
            else -> emit(Event.ERROR)
        }
    }

    /**
     * [complete] means [purchases] is everything the account owns, so a pack
     * that is missing was refunded or belongs to another account.
     */
    private fun handlePurchases(purchases: List<Purchase>, fromUser: Boolean, complete: Boolean) {
        val context = appContext ?: return
        val wasOwned = isOwnedOnPlay(context)
        val wasPending = packPending
        var packBought = false
        var packWaiting = false
        for (purchase in purchases) {
            val products = purchase.products
            when (purchase.purchaseState) {
                Purchase.PurchaseState.PURCHASED -> {
                    if (PACK in products) {
                        packBought = true
                        // Unacknowledged purchases are refunded by Google after 3 days.
                        if (!purchase.isAcknowledged) acknowledge(purchase)
                    }
                    if (products.any { it in TIPS }) consume(purchase)
                }
                Purchase.PurchaseState.PENDING -> {
                    if (PACK in products) packWaiting = true
                    if (fromUser) emit(Event.PENDING)
                }
            }
        }

        val owned = if (complete) packBought else wasOwned || packBought
        packPending = !owned && (if (complete) packWaiting else packWaiting || wasPending)
        if (owned != wasOwned) {
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(KEY_OWNED, owned).apply()
            if (!isSupporter(context)) Cosmetics.resetToFree(context)
        }
        notifyChanged()
        if (owned && !wasOwned && (fromUser || wasPending)) emit(Event.THANKS_PACK)
    }

    private fun isOwnedOnPlay(context: Context): Boolean =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(KEY_OWNED, false)

    private fun acknowledge(purchase: Purchase) {
        val params = AcknowledgePurchaseParams.newBuilder().setPurchaseToken(purchase.purchaseToken).build()
        // A failure is retried by the next refresh(), which runs on every visit to the home screen.
        client?.acknowledgePurchase(params) { }
    }

    private fun consume(purchase: Purchase) {
        val params = ConsumeParams.newBuilder().setPurchaseToken(purchase.purchaseToken).build()
        client?.consumeAsync(params) { result, _ ->
            if (result.responseCode == BillingClient.BillingResponseCode.OK) {
                main.post { emit(Event.THANKS_TIP) }
            }
        }
    }

    private fun notifyChanged() {
        for (listener in listeners.toList()) listener.onSupportChanged()
    }

    private fun emit(event: Event) {
        for (listener in listeners.toList()) listener.onSupportEvent(event)
    }
}
