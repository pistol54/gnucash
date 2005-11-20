/********************************************************************\
 * druid-loan.c : A Gnome Druid for setting up loan-repayment       *
 *     scheduled transactions.                                      *
 * Copyright (C) 2002 Joshua Sled <jsled@asynchronous.org>          *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
\********************************************************************/

#include "config.h"

#include <gnome.h>
#include <glib/gi18n.h>
#include <string.h>
#include <glade/glade.h>
#include <math.h>

#include "druid-loan.h"

#include "SchedXaction.h"
#include "SX-book.h"
#include "SX-book-p.h"
#include "SX-ttinfo.h"
#include "druid-utils.h"
#include "gnc-book.h"
#include "gnc-amount-edit.h"
#include "gnc-account-sel.h"
#include "gnc-date.h"
#include "gnc-exp-parser.h"
#include "gnc-component-manager.h"
#include "dialog-utils.h"
#include "Account.h"
#include "FreqSpec.h"
#include "gnc-ui.h"
#include "gnc-gdate-utils.h"
#include "gnc-gui-query.h"
#include "gnc-ui-util.h"
#include "gnc-frequency.h"
#include "gnc-engine.h"

#define DIALOG_LOAN_DRUID_CM_CLASS "druid-loan-setup"

#define SX_GLADE_FILE "sched-xact.glade"
#define LOAN_DRUID_WIN_GLADE_NAME "loan_druid_win"
#define LOAN_DRUID_GLADE_NAME     "loan_druid"

#define PG_INTRO "loan_intro_pg"
#define PG_INFO "loan_info_pg"
#  define PARAM_TABLE      "param_table"
#  define ORIG_PRINC_ENTRY "orig_princ_ent"
#  define IRATE_SPIN       "irate_spin"
#  define VAR_CONTAINER    "type_freq_frame"
#  define LENGTH_SPIN      "len_spin"
#  define LENGTH_OPT       "len_opt"
#  define REMAIN_SPIN      "rem_spin"
#define PG_OPTS "loan_opts_pg"
#  define OPT_CONTAINER        "opt_vbox"
#  define OPT_ESCROW           "opt_escrow_cb"
#  define OPT_ESCROW_CONTAINER "opt_escrow_hbox"
#define PG_REPAYMENT "repayment_pg"
#  define TXN_NAME       "txn_title"
#  define REPAY_TABLE    "repay_table"
#  define AMOUNT_ENTRY   "amount_ent"
#  define FREQ_CONTAINER "freq_frame"
#define PG_PAYMENT "payment_pg"
#  define PAY_TXN_TITLE      "pay_txn_title"
#  define PAY_AMT_ENTRY      "pay_amt_ent"
#  define PAY_TABLE          "pay_table"
#  define PAY_USE_ESCROW     "pay_use_escrow"
#  define PAY_SPEC_SRC_ACCT  "pay_specify_source"
#  define PAY_FROM_ACCT_LBL  "pay_from_account_label"
#  define PAY_ESC_TO_LBL     "pay_escrow_to_label"
#  define PAY_ESC_FROM_LBL   "pay_escrow_from_label"
#  define PAY_TXN_PART_RB    "pay_txn_part_rb"
#  define PAY_UNIQ_FREQ_RB   "pay_uniq_freq_rb"
#  define PAY_FREQ_CONTAINER "pay_freq_align"
#define PG_REVIEW "review_pg"
#  define REV_SCROLLWIN      "rev_scrollwin"
#  define REV_DATE_FRAME     "rev_date_frame"
#  define REV_RANGE_OPT      "rev_range_opt"
#  define REV_RANGE_TABLE    "rev_date_range_table"
#define PG_COMMIT "commit_pg"

#define OPT_VBOX_SPACING 2

typedef enum {
        CURRENT_YEAR,
        NOW_PLUS_ONE,
        WHOLE_LOAN,
        CUSTOM
} REV_RANGE_OPTS;

static QofLogModule log_module = GNC_MOD_SX;

/**
 * TODO/fixme:
 * . param account selection should fill in orig/cur principal amounts from
 *   the books.
 * . initialize type freq to monthly.
 * . if LoanType <- !FIXED
 *   . Frequency <- sensitive
 **/

struct LoanDruidData_;

/**
 * The data relating to a single "repayment option" -- a potential
 * [sub-]transaction in the repayment.
 **/
typedef struct RepayOptData_ {
        gboolean enabled;
        char *name; /* { "insurance", "pmi", "taxes", ... } */
        char *txnMemo;
        float amount;
        gboolean throughEscrowP;
        gboolean specSrcAcctP;
        Account *to;
        Account *from; /* If NULL { If throughEscrowP, then through escrowAcct };
                        * else: undefined. */
        FreqSpec *fs;  /* If NULL, part of repayment; otherwise: defined
                        * here. */
        GDate *startDate;
} RepayOptData;

/**
 * The default repayment options data.
 **/
typedef struct RepayOptDataDefault_ {
        char *name;
        char *defaultTxnMemo;
        gboolean escrowDefault;
        gboolean specSrcAcctDefault;
} RepayOptDataDefault;

static RepayOptDataDefault REPAY_DEFAULTS[] = {
     /* { name, default txn memo, throughEscrowP, specSrcAcctP } */
     { "Taxes",         "Tax Payment",           TRUE,  FALSE },
     { "Insurance",     "Insurance Payment",     TRUE,  FALSE  },
     { "PMI",           "PMI Payment",           TRUE,  FALSE  },
     { "Other Expense", "Miscellaneous Payment", FALSE, FALSE },
     { NULL }
};

/**
 * The UI-side storage of the repayment options.
 **/
typedef struct RepayOptUI_ {
        /* must be stated this way [instead of 'LoanDruidData*'] because of
         * forward decl. */
        struct LoanDruidData_ *ldd;
        GtkCheckButton *optCb;
        GtkCheckButton *escrowCb;
        RepayOptData *optData;
} RepayOptUIData;

typedef enum {
        FIXED = 0,
        VARIABLE,
        VARIABLE_3_1 = VARIABLE,
        VARIABLE_5_1,
        VARIABLE_7_1,
        VARIABLE_10_1,
        /* ... FIXME */
} LoanType;

typedef enum {
        MONTHS = 0,
        YEARS
} PeriodSize;

/**
 * A transient struct used to collate the GDate and the gnc_numeric row-data
 * for the repayment review schedule.  numCells is an array of gnc_numerics,
 * with a length of the LoanData.revNumPmts.
 **/
typedef struct rev_repayment_row {
        GDate date;
        gnc_numeric *numCells;
} RevRepaymentRow;

/**
 * Data about a loan repayment.
 **/
typedef struct LoanData_ {
        Account *primaryAcct;
        gnc_numeric principal;
        float interestRate;
        LoanType type;
        FreqSpec *loanFreq;
        GDate *startDate;
        GDate *varStartDate;
        int numPer;
        PeriodSize perSize;
        int numMonRemain;

        char *repMemo;
        char *repAmount;
        Account *repFromAcct;
        Account *repPriAcct;
        Account *repIntAcct;
        Account *escrowAcct;
        FreqSpec *repFreq;
        GDate *repStartDate;

        int repayOptCount;
        RepayOptData **repayOpts;

        /* Data concerning the review of repayments. */
        int revNumPmts;
        int revRepayOptToColMap[ (sizeof(REPAY_DEFAULTS)
                                  / sizeof(RepayOptDataDefault))
                                 - 1 ];
        GList *revSchedule;
} LoanData;

/**
 * The UI-side storage of the loan druid data.
 **/
typedef struct LoanDruidData_ {
        GladeXML *gxml;
        GtkWidget *dialog;
        GnomeDruid *druid;

        LoanData ld;
        /* The UI-side storage of repayment data; this is 1:1 with the array
         * in LoanData */
        RepayOptUIData **repayOptsUI;

        /* Current index of the payment opt for multiplexing the 'payment'
         * page. */
        int currentIdx;
        
        /* widgets */
        /* prm = params */
        GtkTable      *prmTable;
        GNCAccountSel *prmAccountGAS;
        GNCAmountEdit *prmOrigPrincGAE;
        GtkSpinButton *prmIrateSpin;
        GtkOptionMenu *prmType;
        GtkFrame      *prmVarFrame;
        GNCFrequency  *prmVarGncFreq;
        GNCDateEdit   *prmStartDateGDE;
        GtkSpinButton *prmLengthSpin;
        GtkOptionMenu *prmLengthType;
        GtkSpinButton *prmRemainSpin;

        /* opt = options */
        GtkVBox        *optVBox;
        GtkCheckButton *optEscrowCb;
        GtkHBox        *optEscrowHBox;
        GNCAccountSel  *optEscrowGAS;

        /* rep = repayment */
        GtkEntry      *repTxnName;
        GtkTable      *repTable;
        GtkEntry      *repAmtEntry;
        GNCAccountSel *repAssetsFromGAS;
        GNCAccountSel *repPrincToGAS;
        GNCAccountSel *repIntToGAS;
        GtkFrame      *repFreqFrame;
        GNCFrequency  *repGncFreq;

        /* pay = payment[s] */
        GtkEntry         *payTxnName;
        GtkEntry         *payAmtEntry;
        GNCAccountSel    *payAcctFromGAS;
        GNCAccountSel    *payAcctEscToGAS;
        GNCAccountSel    *payAcctEscFromGAS;
        GNCAccountSel    *payAcctToGAS;
        GtkTable         *payTable;
        GtkCheckButton   *payUseEscrow;
        GtkCheckButton   *paySpecSrcAcct;
        GtkLabel         *payAcctFromLabel;
        GtkLabel         *payEscToLabel;
        GtkLabel         *payEscFromLabel;
        GtkRadioButton   *payTxnFreqPartRb;
        GtkRadioButton   *payTxnFreqUniqRb;
        GtkAlignment     *payFreqAlign;
        GNCFrequency     *payGncFreq;

        /* rev = review */
        GtkOptionMenu     *revRangeOpt;
        GtkFrame          *revDateFrame;
        GtkTable          *revTable;
        GNCDateEdit       *revStartDate;
        GNCDateEdit       *revEndDate;
        GtkScrolledWindow *revScrollWin;
        GtkCList          *revCL;
} LoanDruidData;

/**
 * A transient structure to contain SX details during the creation process.
 **/
typedef struct toCreateSX_
{
  /** The name of the SX */
  gchar *name;
  /** The start, last-occurred and end dates. */
  GDate start, last, end;
  /** The SX FreqSpec */
  FreqSpec *freq;
  /** The current 'instance-num' count. */
  gint instNum;
  /** The main/source transaction being created. */
  TTInfo *mainTxn;
  /** The optional escrow transaction being created. */
  TTInfo *escrowTxn;
} toCreateSX;

static void gnc_loan_druid_data_init( LoanDruidData *ldd );
static void gnc_loan_druid_get_widgets( LoanDruidData *ldd );

static void ld_close_handler( LoanDruidData *ldd );
static void ld_destroy( GtkObject *o, gpointer ud );

static void ld_cancel_check( GnomeDruid *gd, LoanDruidData *ldd );
static void ld_prm_type_changed( GtkWidget *w, gint index, gpointer ud );
static void ld_calc_upd_rem_payments( GtkWidget *w, gpointer ud );

static void ld_escrow_toggle( GtkToggleButton *tb, gpointer ud );
static void ld_opt_toggled( GtkToggleButton *tb, gpointer ud );
static void ld_opt_consistency( GtkToggleButton *tb, gpointer ud );
static void ld_escrow_toggled( GtkToggleButton *tb, gpointer ud );

static void ld_pay_freq_toggle( GtkToggleButton *tb, gpointer ud );

static void ld_pay_use_esc_setup( LoanDruidData *ldd, gboolean newState );
static void ld_pay_use_esc_toggle( GtkToggleButton *tb, gpointer ud );
static void ld_pay_spec_src_setup( LoanDruidData *ldd, gboolean newState );
static void ld_pay_spec_src_toggle( GtkToggleButton *tb, gpointer ud );

static void ld_get_loan_range( LoanDruidData *ldd,
                               GDate *start,
                               GDate *end );

static void ld_rev_recalc_schedule( LoanDruidData *ldd );
static void ld_rev_range_opt_changed( GtkButton *b, gpointer ud );
static void ld_rev_range_changed( GNCDateEdit *gde, gpointer ud );
static void ld_rev_get_dates( LoanDruidData *ldd,
                              GDate *start,
                              GDate *end );
static void ld_rev_update_clist( LoanDruidData *ldd,
                                 GDate *start,
                                 GDate *end );
static void ld_rev_clist_allocate_col_widths( GtkWidget *w,
                                              GtkAllocation *alloc,
                                              gpointer user_data );
static void ld_rev_sched_list_free( gpointer data, gpointer user_data );
static void ld_rev_hash_to_list( gpointer key,
                                 gpointer val,
                                 gpointer user_data );
static void ld_rev_hash_free_date_keys( gpointer key,
                                        gpointer val,
                                        gpointer user_data );

static void ld_get_pmt_formula( LoanDruidData *ldd, GString *gstr );
static void ld_get_ppmt_formula( LoanDruidData *ldd, GString *gstr );
static void ld_get_ipmt_formula( LoanDruidData *ldd, GString *gstr );

static gboolean ld_info_save( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static void     ld_info_prep( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static gboolean ld_opts_tran( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static void     ld_opts_prep( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static gboolean ld_rep_next ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static void     ld_rep_prep ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static gboolean ld_rep_back ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static gboolean ld_pay_next ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static void     ld_pay_prep ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static gboolean ld_pay_back ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static gboolean ld_rev_next ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static void     ld_rev_prep ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static gboolean ld_rev_back ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static gboolean ld_com_next ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static void     ld_com_prep ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static gboolean ld_com_back ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );
static void     ld_com_fin  ( GnomeDruidPage *gdp, gpointer arg1, gpointer ud );

static void ld_create_sxes( LoanDruidData *ldd );
static gint ld_find_ttsplit_with_acct( gconstpointer elt,
                                       gconstpointer crit );
static void ld_create_sx_from_tcSX( LoanDruidData *ldd, toCreateSX *tcSX );
static void ld_tcSX_free( gpointer data, gpointer user_data );

struct LoanDruidData_*
gnc_ui_sx_loan_druid_create(void)
{
        int i;
        LoanDruidData *ldd;

        ldd = g_new0( LoanDruidData, 1 );

        gnc_loan_druid_data_init( ldd );

        ldd->gxml   = gnc_glade_xml_new( SX_GLADE_FILE, LOAN_DRUID_WIN_GLADE_NAME );
        ldd->dialog = glade_xml_get_widget( ldd->gxml, LOAN_DRUID_WIN_GLADE_NAME );
        ldd->druid  = GNOME_DRUID(glade_xml_get_widget( ldd->gxml,
                                                        LOAN_DRUID_GLADE_NAME ));
	gnc_druid_set_colors (ldd->druid);

        /* get pointers to the various widgets */
        gnc_loan_druid_get_widgets( ldd );
        
        /* non-gladeable widget setup */
        {
                int i;
                // LIABILITY
                GList *liabilityAcct;
                // BANK, CASH, CREDIT, ASSET + LIABILITY
                GList *paymentFromAccts;
                // EXPENSE, LIABILITY, + payment-froms.
                GList *paymentToAccts;
                int fromLen = 5;
                GNCAccountType paymentFroms[] = { BANK, CASH, CREDIT, ASSET, LIABILITY };
                int toLen = 2;
                GNCAccountType paymentTos[] = { EXPENSE };

                liabilityAcct = NULL;
                paymentFromAccts = NULL;
                paymentToAccts = NULL;

                liabilityAcct = g_list_append( liabilityAcct,
                                               GINT_TO_POINTER( LIABILITY ) );
                for ( i = 0; i < fromLen; i++ )
                {
                        paymentFromAccts
                                = g_list_append( paymentFromAccts,
                                                 GINT_TO_POINTER( paymentFroms[i] ) );
                        paymentToAccts
                                = g_list_append( paymentToAccts,
                                                 GINT_TO_POINTER( paymentFroms[i] ) );
                }

                for ( i = 0; i < toLen; i++ )
                {
                        paymentToAccts = g_list_append( paymentToAccts,
                                                        GINT_TO_POINTER( paymentTos[i] ) );
                }

                /* All of the GncAccountSel[ectors]... */
                {
                        int i;
                        GtkAlignment *a;
                        /* "gas" == GncAccountSel */
                        struct gas_in_tables_data {
                                GNCAccountSel **loc;
                                GtkTable *table;
                                gboolean newAcctAbility;
                                int left, right, top, bottom;
                                GList *allowableAccounts;
                        } gas_data[] = {
                                /* These ints are the GtkTable boundries */
                                { &ldd->prmAccountGAS,     ldd->prmTable, TRUE,  1, 4, 0, 1, liabilityAcct },
                                { &ldd->repAssetsFromGAS,  ldd->repTable, TRUE,  1, 4, 2, 3, paymentFromAccts },
                                { &ldd->repPrincToGAS,     ldd->repTable, TRUE,  1, 2, 3, 4, paymentToAccts  },
                                { &ldd->repIntToGAS,       ldd->repTable, TRUE,  3, 4, 3, 4, paymentToAccts },
                                { &ldd->payAcctFromGAS,    ldd->payTable, TRUE,  1, 2, 4, 5, paymentFromAccts },
                                { &ldd->payAcctEscToGAS,   ldd->payTable, FALSE, 3, 4, 4, 5, paymentToAccts },
                                { &ldd->payAcctEscFromGAS, ldd->payTable, FALSE, 1, 2, 5, 6, paymentFromAccts },
                                { &ldd->payAcctToGAS,      ldd->payTable, TRUE,  3, 4, 5, 6, paymentToAccts },
                                { NULL }
                        };

                        /* left-aligned, 25%-width */
                        a = GTK_ALIGNMENT(gtk_alignment_new( 0.0, 0.5, 0.25, 1.0 ));
                        ldd->prmOrigPrincGAE = GNC_AMOUNT_EDIT(gnc_amount_edit_new());
                        gtk_container_add( GTK_CONTAINER(a), GTK_WIDGET(ldd->prmOrigPrincGAE) );
                        gtk_table_attach( ldd->prmTable, GTK_WIDGET(a),
                                          1, 4, 1, 2,
                                          GTK_EXPAND | GTK_FILL,
                                          GTK_EXPAND | GTK_FILL, 2, 2 );

                        for ( i=0; gas_data[i].loc != NULL; i++ ) {
                                GNCAccountSel *gas;

                                a = GTK_ALIGNMENT(gtk_alignment_new( 0.0, 0.5, 0.25, 1.0 ));
                                gas = GNC_ACCOUNT_SEL(gnc_account_sel_new());
                                gnc_account_sel_set_new_account_ability(
                                        gas, gas_data[i].newAcctAbility );
                                if ( gas_data[i].allowableAccounts != NULL ) {
                                        gnc_account_sel_set_acct_filters(
                                                gas, gas_data[i].allowableAccounts );
                                }
                                gtk_container_add( GTK_CONTAINER(a),
                                                   GTK_WIDGET(gas) );
                                gtk_table_attach( gas_data[i].table,
                                                  GTK_WIDGET(a),
                                                  gas_data[i].left,
                                                  gas_data[i].right,
                                                  gas_data[i].top,
                                                  gas_data[i].bottom,
                                                  GTK_EXPAND | GTK_FILL,
                                                  GTK_EXPAND | GTK_FILL, 2, 2 );
                                *(gas_data[i].loc) = gas;
                        }
                }

                /* Setup the payment page always-insensitive GASes */
                gtk_widget_set_sensitive( GTK_WIDGET(ldd->payAcctEscToGAS),   FALSE );
                gtk_widget_set_sensitive( GTK_WIDGET(ldd->payAcctEscFromGAS), FALSE );

                /* The GNCDateEdit[s] */
                {
                        /* "gde" == GNCDateEdit */
                        struct gde_in_tables_data {
                                GNCDateEdit **loc;
                                GtkTable *table;
                                int left, right, top, bottom;
                        } gde_data[] = {
                                /* These ints are the GtkTable boundries */
                                { &ldd->prmStartDateGDE, ldd->prmTable, 1, 2, 4, 5 },
                                { &ldd->revStartDate,    ldd->revTable, 1, 2, 0, 1 },
                                { &ldd->revEndDate,      ldd->revTable, 1, 2, 1, 2 },
                                { NULL }
                        };

                        for ( i=0; gde_data[i].loc != NULL; i++ ) {
                                *gde_data[i].loc =
                                        GNC_DATE_EDIT(
                                                gnc_date_edit_new( time(NULL),
                                                                   FALSE, FALSE ) );
                                gtk_table_attach( gde_data[i].table,
                                                  GTK_WIDGET( *gde_data[i].loc ),
                                                  gde_data[i].left,
                                                  gde_data[i].right,
                                                  gde_data[i].top,
                                                  gde_data[i].bottom,
                                                  (GTK_EXPAND | GTK_FILL),
                                                  GTK_FILL, 0, 0 );
                        }

                }

                gtk_widget_set_sensitive( GTK_WIDGET(ldd->prmVarFrame), FALSE );
                {
                        GtkAlignment *a;
                        GNCOptionInfo typeOptInfo[] = {
                                { _("Fixed"), _("A Fixed-Rate loan"), ld_prm_type_changed, ldd },
                                /* Translators: ARM = Adjustable Rate Mortgage */
                                { _("3/1 Year"),   _("A 3/1 Year ARM"),         ld_prm_type_changed, ldd },
                                { _("5/1 Year"),   _("A 5/1 Year ARM"),         ld_prm_type_changed, ldd },
                                { _("7/1 Year"),   _("A 7/1 Year ARM"),         ld_prm_type_changed, ldd },
                                { _("10/1 Year"),  _("A 10/1 Year ARM"),        ld_prm_type_changed, ldd },
                        };
                        ldd->prmType =
                                GTK_OPTION_MENU( gnc_build_option_menu( typeOptInfo, 5 ) );
                        a = GTK_ALIGNMENT( gtk_alignment_new( 0.0, 0.5, 0.25, 1.0 ) );
                        gtk_container_add( GTK_CONTAINER(a), GTK_WIDGET(ldd->prmType) );
                        gtk_table_attach( ldd->prmTable, GTK_WIDGET(a),
                                          3, 4, 2, 3,
                                          0, 0, 2, 2 );
                }

                {
                        GtkAdjustment *a;

                        /* 8.0 [%], range of 0.005..100.0 with ticks at 0.001[%]. */
                        a = GTK_ADJUSTMENT(gtk_adjustment_new( 8.0, 0.001,
                                                               100.0, 0.001,
                                                               1.0, 1.0 ));
                        gtk_spin_button_set_adjustment( ldd->prmIrateSpin, a );
                        gtk_spin_button_set_value( ldd->prmIrateSpin, 8.00 );
                        gtk_spin_button_set_snap_to_ticks( ldd->prmIrateSpin,
                                                           TRUE );

                        a = GTK_ADJUSTMENT(gtk_adjustment_new( 360, 1,
                                                               9999, 1,
                                                               12, 12 ));
                        gtk_spin_button_set_adjustment( ldd->prmLengthSpin, a );
                        gtk_signal_connect( GTK_OBJECT(ldd->prmLengthSpin), "changed",
                                            GTK_SIGNAL_FUNC( ld_calc_upd_rem_payments ),
                                            ldd );
                        gtk_signal_connect( GTK_OBJECT(ldd->prmStartDateGDE),
                                            "date-changed",
                                            GTK_SIGNAL_FUNC( ld_calc_upd_rem_payments ),
                                            ldd );
                        gtk_signal_connect( GTK_OBJECT(ldd->prmLengthSpin), "changed",
                                            GTK_SIGNAL_FUNC( ld_calc_upd_rem_payments ),
                                            ldd );
                        gtk_signal_connect( GTK_OBJECT(gtk_option_menu_get_menu(
                                                               ldd->prmLengthType)),
                                            "selection-done",
                                            GTK_SIGNAL_FUNC( ld_calc_upd_rem_payments ),
                                            ldd );

                        a = GTK_ADJUSTMENT(gtk_adjustment_new( 360, 1,
                                                               9999, 1,
                                                               12, 12 ));
                        gtk_spin_button_set_adjustment( ldd->prmRemainSpin, a );
                }
               
                gnc_option_menu_init( GTK_WIDGET(ldd->prmType) );
                gnc_option_menu_init( GTK_WIDGET(ldd->prmLengthType) );
                gnc_option_menu_init( GTK_WIDGET(ldd->revRangeOpt) );

                gtk_signal_connect( GTK_OBJECT(ldd->optEscrowCb), "toggled",
                                    GTK_SIGNAL_FUNC(ld_escrow_toggle), ldd );
                gtk_widget_set_sensitive( GTK_WIDGET(ldd->optEscrowHBox), FALSE );
                ldd->optEscrowGAS = GNC_ACCOUNT_SEL(gnc_account_sel_new());
                gnc_account_sel_set_new_account_ability( ldd->optEscrowGAS, TRUE );
                gtk_container_add( GTK_CONTAINER(ldd->optEscrowHBox),
                                   GTK_WIDGET(ldd->optEscrowGAS) );

                {
                        /* . Each RepayOpt gets an "entry" in the optContainer.
                         * . Each "entry" is a 2-line vbox containing:
                         *   . The checkbox for the option itself
                         *   . an alignment-contained sub-checkbox for "through the
                         *     escrow account".
                         *   . Hook up each to bit-twiddling the appropriate line.
                         */

                        RepayOptUIData *rouid;
                        GtkVBox *vb;
                        GtkAlignment *optAlign, *subOptAlign;
                        GString *str;

                        str = g_string_sized_new( 32 );

                        for ( i=0; i<ldd->ld.repayOptCount; i++ ) {
                                rouid = ldd->repayOptsUI[i];
                                vb = GTK_VBOX(gtk_vbox_new( FALSE, OPT_VBOX_SPACING ));

                                /* Add payment checkbox. */

                                /* Translators: %s is "Taxes",
                                 * "Insurance", or similar. */
                                g_string_printf( str, _("... pay \"%s\"?"),
                                                 rouid->optData->name );
                                rouid->optCb =
                                        GTK_CHECK_BUTTON(
                                                gtk_check_button_new_with_label(
                                                        str->str ));
                                gtk_box_pack_start( GTK_BOX(vb),
                                                    GTK_WIDGET(rouid->optCb),
                                                    FALSE, FALSE, 2 );
                                rouid->escrowCb =
                                        GTK_CHECK_BUTTON(
                                                gtk_check_button_new_with_label(
                                                        _("via Escrow account?") ));
                                gtk_widget_set_sensitive(
                                        GTK_WIDGET(rouid->escrowCb),
                                        FALSE );
                                subOptAlign =
                                        GTK_ALIGNMENT(
                                                gtk_alignment_new(
                                                        0.5, 0.5, 0.75, 1.0 ));
                                gtk_container_add( GTK_CONTAINER(subOptAlign),
                                                   GTK_WIDGET(rouid->escrowCb) );
                                gtk_box_pack_start( GTK_BOX(vb), GTK_WIDGET(subOptAlign),
                                                    FALSE, FALSE, 2 );

                                gtk_signal_connect( GTK_OBJECT( rouid->optCb ), "toggled",
                                                    GTK_SIGNAL_FUNC(ld_opt_toggled),
                                                    (gpointer)rouid );
                                gtk_signal_connect( GTK_OBJECT( rouid->optCb ), "toggled",
                                                    GTK_SIGNAL_FUNC(ld_opt_consistency),
                                                    (gpointer)rouid );
                                gtk_signal_connect( GTK_OBJECT( rouid->escrowCb ), "toggled",
                                                    GTK_SIGNAL_FUNC(ld_escrow_toggled),
                                                    (gpointer)rouid );

                                optAlign = GTK_ALIGNMENT(gtk_alignment_new( 0.5, 0.5, 0.75, 1.0 ));
                                gtk_container_add( GTK_CONTAINER(optAlign),
                                                   GTK_WIDGET(vb) );
                                gtk_box_pack_start( GTK_BOX(ldd->optVBox), GTK_WIDGET(optAlign),
                                                    FALSE, FALSE, 2 );
                                gtk_widget_show_all( GTK_WIDGET(optAlign) );
                        }
                        g_string_free( str, TRUE );
                }

                gtk_signal_connect( GTK_OBJECT(ldd->payUseEscrow), "toggled",
                                    GTK_SIGNAL_FUNC(ld_pay_use_esc_toggle),
                                    (gpointer)ldd );
                gtk_signal_connect( GTK_OBJECT(ldd->paySpecSrcAcct), "toggled",
                                    GTK_SIGNAL_FUNC(ld_pay_spec_src_toggle),
                                    (gpointer)ldd );
                gtk_signal_connect( GTK_OBJECT(ldd->payTxnFreqPartRb), "toggled",
                                    GTK_SIGNAL_FUNC(ld_pay_freq_toggle), (gpointer)ldd );
                gtk_signal_connect( GTK_OBJECT(ldd->payTxnFreqUniqRb), "toggled",
                                    GTK_SIGNAL_FUNC(ld_pay_freq_toggle), (gpointer)ldd );

                ldd->prmVarGncFreq =
                        GNC_FREQUENCY(gnc_frequency_new( NULL, NULL ));
                gtk_container_add( GTK_CONTAINER(ldd->prmVarFrame),
                                   GTK_WIDGET(ldd->prmVarGncFreq) );

                ldd->repGncFreq =
                        GNC_FREQUENCY(gnc_frequency_new( NULL, NULL ));
                gtk_container_add( GTK_CONTAINER(ldd->repFreqFrame),
                                   GTK_WIDGET(ldd->repGncFreq) );

                ldd->payGncFreq =
                        GNC_FREQUENCY(gnc_frequency_new( NULL, NULL ));
                gtk_container_add( GTK_CONTAINER(ldd->payFreqAlign),
                                   GTK_WIDGET(ldd->payGncFreq) );
        }

        /* Review page widget setup. */
        {
                gtk_signal_connect( GTK_OBJECT(gtk_option_menu_get_menu(
                                                       ldd->revRangeOpt)),
                                    "selection-done",
                                    GTK_SIGNAL_FUNC( ld_rev_range_opt_changed ),
                                    ldd );
                gtk_signal_connect( GTK_OBJECT(ldd->revStartDate),
                                    "date-changed",
                                    GTK_SIGNAL_FUNC( ld_rev_range_changed ),
                                    ldd );
                gtk_signal_connect( GTK_OBJECT(ldd->revEndDate),
                                    "date-changed",
                                    GTK_SIGNAL_FUNC( ld_rev_range_changed ),
                                    ldd );
                
        }

        {
                static struct {
                        char *pageName;
                        gboolean (*nextFn)();
                        void     (*prepFn)();
                        gboolean (*backFn)();
                        void     (*finishFn)();
                        /* cancel is handled by the druid itself. */
                } DRUID_HANDLERS[] = {
                        { PG_INFO,      ld_info_save, ld_info_prep, ld_info_save, NULL },
                        { PG_OPTS,      ld_opts_tran, ld_opts_prep, ld_opts_tran, NULL },
                        { PG_REPAYMENT, ld_rep_next,  ld_rep_prep,  ld_rep_back,  NULL },
                        { PG_PAYMENT,   ld_pay_next,  ld_pay_prep,  ld_pay_back,  NULL },
                        { PG_REVIEW,    ld_rev_next,  ld_rev_prep,  ld_rev_back,  NULL },
                        { PG_COMMIT,    ld_com_next,  ld_com_prep,  ld_com_back,  ld_com_fin },
                        { NULL }
                };

                /* setup page-transition handlers */
                /* setup druid-global handler for cancel */
                gtk_signal_connect( GTK_OBJECT(ldd->druid), "cancel",
                                    GTK_SIGNAL_FUNC(ld_cancel_check),
                                    (gpointer)ldd );
                /* FIXME: this is substantially similar to the code in
                 * dialog-sxsincelast.c ... it should probably be factored out
                 * somewhere. */
                for ( i=0; DRUID_HANDLERS[i].pageName != NULL; i++ ) {
                        GtkWidget *pg;

                        pg = glade_xml_get_widget( ldd->gxml,
                                                   DRUID_HANDLERS[i].pageName );
                        if ( DRUID_HANDLERS[i].prepFn ) {
                                gtk_signal_connect( GTK_OBJECT(pg), "prepare",
                                                    GTK_SIGNAL_FUNC(DRUID_HANDLERS[i].
                                                                    prepFn),
                                                    ldd);
                        }
                        if ( DRUID_HANDLERS[i].backFn ) {
                                gtk_signal_connect( GTK_OBJECT(pg), "back",
                                                    GTK_SIGNAL_FUNC(DRUID_HANDLERS[i].
                                                                    backFn),
                                                    ldd);
                        }
                        if ( DRUID_HANDLERS[i].nextFn ) {
                                gtk_signal_connect( GTK_OBJECT(pg), "next",
                                                    GTK_SIGNAL_FUNC(DRUID_HANDLERS[i].
                                                                    nextFn),
                                                    ldd);
                        }
                        if ( DRUID_HANDLERS[i].finishFn ) {
                                gtk_signal_connect( GTK_OBJECT(pg), "finish",
                                                    GTK_SIGNAL_FUNC(DRUID_HANDLERS[i].
                                                                    finishFn),
                                                    ldd);
                        }
                }
        }

        gnc_register_gui_component( DIALOG_LOAN_DRUID_CM_CLASS,
                                    NULL, /* no refresh handler */
                                    (GNCComponentCloseHandler)ld_close_handler,
                                    ldd );

        gtk_signal_connect( GTK_OBJECT(ldd->dialog), "destroy",
                            GTK_SIGNAL_FUNC(ld_destroy),
                            ldd );

        gtk_widget_show_all( ldd->dialog );
        return ldd;
}

static
void
gnc_loan_druid_data_init( LoanDruidData *ldd )
{
        int i, optCount;
        RepayOptData *optData;

        /* get the count of repayment defaults. */
        for ( optCount=0; REPAY_DEFAULTS[optCount].name != NULL; optCount++ )
                ;

        ldd->currentIdx = -1;

        ldd->ld.principal = gnc_numeric_zero();
        ldd->ld.startDate = g_date_new();
        ldd->ld.varStartDate = g_date_new();
        g_date_set_time( ldd->ld.startDate, time(NULL) );
        ldd->ld.loanFreq  = xaccFreqSpecMalloc( gnc_get_current_book() );
        ldd->ld.repFreq   = xaccFreqSpecMalloc( gnc_get_current_book() );
        xaccFreqSpecSetMonthly( ldd->ld.repFreq, ldd->ld.startDate, 1 );
        xaccFreqSpecSetUIType( ldd->ld.repFreq, UIFREQ_MONTHLY );

        ldd->ld.repMemo = g_strdup( _("Loan") );
        ldd->ld.repAmount = NULL;
        ldd->ld.repStartDate = g_date_new();
        ldd->ld.repayOptCount = optCount;
        ldd->ld.repayOpts = g_new0( RepayOptData*, optCount );
        /* copy all the default lines into the LDD */
        ldd->repayOptsUI = g_new0( RepayOptUIData*, optCount );
        for ( i=0; i < optCount; i++ ) {
                g_assert( REPAY_DEFAULTS[i].name != NULL );

                ldd->repayOptsUI[i] = g_new0( RepayOptUIData, 1 );
                ldd->repayOptsUI[i]->ldd = ldd;

                optData = ldd->ld.repayOpts[i]
                        = ldd->repayOptsUI[i]->optData
                        = g_new0( RepayOptData, 1 );

                optData->enabled        = FALSE;
                optData->name           = g_strdup( REPAY_DEFAULTS[i].name );
                optData->txnMemo        = g_strdup( REPAY_DEFAULTS[i].
                                                    defaultTxnMemo );
                optData->amount         = 0.0;
                optData->throughEscrowP = REPAY_DEFAULTS[i].escrowDefault;
                optData->specSrcAcctP   = REPAY_DEFAULTS[i].specSrcAcctDefault;
                optData->fs             = NULL;
                optData->startDate      = NULL;
        }
}

static
void
gnc_loan_druid_get_widgets( LoanDruidData *ldd )
{
        g_assert( ldd != NULL );
        g_assert( ldd->gxml != NULL );

        /* Get all widgets */
#define GET_CASTED_WIDGET( cast, name ) \
        (cast( glade_xml_get_widget( ldd->gxml, name ) ))

        /* prm = params */
        ldd->prmTable =
                GET_CASTED_WIDGET( GTK_TABLE,          PARAM_TABLE );
        ldd->prmIrateSpin =
                GET_CASTED_WIDGET( GTK_SPIN_BUTTON,    IRATE_SPIN );
        ldd->prmVarFrame =
                GET_CASTED_WIDGET( GTK_FRAME,          VAR_CONTAINER );
        /* ldd->prmStartDateGDE */
        ldd->prmLengthSpin =
                GET_CASTED_WIDGET( GTK_SPIN_BUTTON,    LENGTH_SPIN );
        ldd->prmLengthType =
                GET_CASTED_WIDGET( GTK_OPTION_MENU,    LENGTH_OPT );
        ldd->prmRemainSpin =
                GET_CASTED_WIDGET( GTK_SPIN_BUTTON,    REMAIN_SPIN );
        
        /* opt = options */
        ldd->optVBox =
                GET_CASTED_WIDGET( GTK_VBOX,           OPT_CONTAINER );
        ldd->optEscrowCb =
                GET_CASTED_WIDGET( GTK_CHECK_BUTTON,   OPT_ESCROW );
        ldd->optEscrowHBox =
                GET_CASTED_WIDGET( GTK_HBOX,           OPT_ESCROW_CONTAINER );

        /* rep = repayment */
        ldd->repTxnName =
                GET_CASTED_WIDGET( GTK_ENTRY,          TXN_NAME );
        ldd->repTable =
                GET_CASTED_WIDGET( GTK_TABLE,          REPAY_TABLE );
        ldd->repAmtEntry =
                GET_CASTED_WIDGET( GTK_ENTRY,          AMOUNT_ENTRY );
        ldd->repFreqFrame =
                GET_CASTED_WIDGET( GTK_FRAME,          FREQ_CONTAINER );

        /* pay = payment[s] */
        ldd->payTxnName =
                GET_CASTED_WIDGET( GTK_ENTRY,          PAY_TXN_TITLE );
        ldd->payAmtEntry =
                GET_CASTED_WIDGET( GTK_ENTRY,          PAY_AMT_ENTRY );
        ldd->payTable =
                GET_CASTED_WIDGET( GTK_TABLE,          PAY_TABLE );
        ldd->payUseEscrow =
                GET_CASTED_WIDGET( GTK_CHECK_BUTTON,   PAY_USE_ESCROW );
        ldd->paySpecSrcAcct =
                GET_CASTED_WIDGET( GTK_CHECK_BUTTON,   PAY_SPEC_SRC_ACCT );
        ldd->payAcctFromLabel =
                GET_CASTED_WIDGET( GTK_LABEL,          PAY_FROM_ACCT_LBL );
        ldd->payEscToLabel =
                GET_CASTED_WIDGET( GTK_LABEL,          PAY_ESC_TO_LBL );
        ldd->payEscFromLabel =
                GET_CASTED_WIDGET( GTK_LABEL,          PAY_ESC_FROM_LBL );
        ldd->payTxnFreqPartRb =
                GET_CASTED_WIDGET( GTK_RADIO_BUTTON,   PAY_TXN_PART_RB );
        ldd->payTxnFreqUniqRb =
                GET_CASTED_WIDGET( GTK_RADIO_BUTTON,   PAY_UNIQ_FREQ_RB );
        ldd->payFreqAlign =
                GET_CASTED_WIDGET( GTK_ALIGNMENT,      PAY_FREQ_CONTAINER );

        /* rev = review */
        ldd->revRangeOpt =
                GET_CASTED_WIDGET( GTK_OPTION_MENU,    REV_RANGE_OPT );
        ldd->revDateFrame =
                GET_CASTED_WIDGET( GTK_FRAME,          REV_DATE_FRAME );
        ldd->revTable =
                GET_CASTED_WIDGET( GTK_TABLE,          REV_RANGE_TABLE );
        /* GNCDateEdit       *revStartDate */
        /* GNCDateEdit       *revEndDate   */
        ldd->revScrollWin =
                GET_CASTED_WIDGET( GTK_SCROLLED_WINDOW, REV_SCROLLWIN );

}

static
void
ld_get_pmt_formula( LoanDruidData *ldd, GString *gstr )
{
        g_assert( ldd != NULL );
        g_assert( gstr != NULL );
        g_string_append_printf( gstr, "pmt( %.5f / 12 : %d : %0.2f : 0 : 0 )",
                                (ldd->ld.interestRate / 100),
                                ( ldd->ld.numPer
                                  * ( ldd->ld.perSize == MONTHS ? 1 : 12 ) ),
                                gnc_numeric_to_double(ldd->ld.principal) );
}

static
void
ld_get_ppmt_formula( LoanDruidData *ldd, GString *gstr )
{
        g_assert( ldd != NULL );
        g_assert( gstr != NULL );
        g_string_printf( gstr, "ppmt( %.5f / 12 : i : %d : %0.2f : 0 : 0 )",
                         (ldd->ld.interestRate / 100),
                         ( ldd->ld.numPer
                           * ( ldd->ld.perSize == MONTHS ? 1 : 12 ) ),
                         gnc_numeric_to_double(ldd->ld.principal));
}

static
void
ld_get_ipmt_formula( LoanDruidData *ldd, GString *gstr )
{
        g_assert( ldd != NULL );
        g_assert( gstr != NULL );
        g_string_printf( gstr, "ipmt( %.5f / 12 : i : %d : %0.2f : 0 : 0 )",
                         (ldd->ld.interestRate / 100),
                         ( ldd->ld.numPer
                           * ( ldd->ld.perSize == MONTHS ? 1 : 12 ) ),
                         gnc_numeric_to_double( ldd->ld.principal ) );
}

static
void
ld_close_handler( LoanDruidData *ldd )
{
        gtk_widget_hide( ldd->dialog );
        gtk_widget_destroy( ldd->dialog );
}

static
void
ld_destroy( GtkObject *o, gpointer ud )
{
        LoanDruidData *ldd;

        ldd = (LoanDruidData*)ud;

        g_assert( ldd );

        gnc_unregister_gui_component_by_data
          (DIALOG_LOAN_DRUID_CM_CLASS, ldd);

        /* free alloc'd mem; cleanup */

        /* repay opts */
        {
                int i;

                g_date_free( ldd->ld.startDate );
                g_date_free( ldd->ld.varStartDate );
                xaccFreqSpecFree( ldd->ld.loanFreq );

                if ( ldd->ld.repMemo )
                        g_free( ldd->ld.repMemo );

                for ( i=0; i<ldd->ld.repayOptCount; i++ ) {
                        RepayOptData *rod = ldd->ld.repayOpts[i];
                        if ( rod->name ) 
                                g_free( rod->name );
                        if ( rod->txnMemo ) 
                                g_free( rod->txnMemo );

                        if ( rod->startDate )
                                g_date_free( rod->startDate );

                        if ( rod->fs )
                                xaccFreqSpecFree( rod->fs );

                        g_free( ldd->ld.repayOpts[i] );
                        g_free( ldd->repayOptsUI[i] );
                }
                g_free( ldd->ld.repayOpts );
                g_free( ldd->repayOptsUI );

                if ( ldd->ld.repAmount )
                        g_free( ldd->ld.repAmount );

                g_date_free( ldd->ld.repStartDate );
        }

        /* review */
        {
                if ( ldd->ld.revSchedule ) {
                        g_list_foreach( ldd->ld.revSchedule,
                                        ld_rev_sched_list_free,
                                        NULL );
                        g_list_free( ldd->ld.revSchedule );
                        ldd->ld.revSchedule = NULL;
                }
        }

        g_free( ldd );
}


static
void
ld_cancel_check( GnomeDruid *gd, LoanDruidData *ldd )
{
        const char *cancelMsg = _( "Are you sure you want to cancel "
                                   "the Mortgage/Loan Setup Druid?" );
        if ( gnc_verify_dialog( ldd->dialog, FALSE, cancelMsg ) ) {
                gnc_close_gui_component_by_data( DIALOG_LOAN_DRUID_CM_CLASS,
                                                 ldd );
        }
}

static
void
ld_prm_type_changed( GtkWidget *w, gint index, gpointer ud )
{
        LoanDruidData *ldd;

        ldd = (LoanDruidData*)ud;
        gtk_widget_set_sensitive( GTK_WIDGET(ldd->prmVarFrame),
                                  index != FIXED );
}

static
void
ld_escrow_toggle( GtkToggleButton *tb, gpointer ud )
{
        int i;
        gboolean newState;
        RepayOptUIData *rouid;
        LoanDruidData *ldd;

        ldd = (LoanDruidData*)ud;
        newState = gtk_toggle_button_get_active(tb);
        gtk_widget_set_sensitive( GTK_WIDGET(ldd->optEscrowHBox), newState );
        /* deal with escrow options. */
        for ( i=0; i<ldd->ld.repayOptCount; i++ ) {
                rouid = ldd->repayOptsUI[i];
                /* If we're going off, then uncheck and desensitize all escrow opts. */
                /* If we're going on, then sensitize all escrow opts. */

                /* prevent the toggle handler from running and "trashing" the
                 * state of the throughEscrowP selection */
                gtk_signal_handler_block_by_func( GTK_OBJECT(rouid->escrowCb),
                                                  GTK_SIGNAL_FUNC(ld_escrow_toggled),
                                                  rouid );
                gtk_toggle_button_set_active(
                        GTK_TOGGLE_BUTTON(rouid->escrowCb),
                        ( newState
                          && gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(rouid->optCb) )
                          && rouid->optData->throughEscrowP ) );
                gtk_widget_set_sensitive(
                        GTK_WIDGET(rouid->escrowCb),
                        newState
                        && gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(rouid->optCb) ) );
                gtk_signal_handler_unblock_by_func( GTK_OBJECT(rouid->escrowCb),
                                                    GTK_SIGNAL_FUNC(ld_escrow_toggled),
                                                    rouid );
                if ( newState ) {
                        rouid->optData->from = ldd->ld.escrowAcct;
                } else {
                        rouid->optData->from = NULL;
                }
        }
}

static
void
ld_opt_toggled( GtkToggleButton *tb, gpointer ud )
{
        RepayOptUIData *rouid;

        rouid = (RepayOptUIData*)ud;
        rouid->optData->enabled = gtk_toggle_button_get_active(tb);
}

static
void
ld_opt_consistency( GtkToggleButton *tb, gpointer ud )
{
        GtkToggleButton *escrowCb;
        RepayOptUIData *rouid;

        rouid = (RepayOptUIData*)ud;
        escrowCb = GTK_TOGGLE_BUTTON(rouid->escrowCb);
        /* make sure the escrow option is only selected if we're active. */
        gtk_toggle_button_set_state( escrowCb,
                                     gtk_toggle_button_get_active(
                                             GTK_TOGGLE_BUTTON(
                                                     rouid->ldd->optEscrowCb) )
                                     && rouid->optData->throughEscrowP
                                     && gtk_toggle_button_get_active(tb) );
        /* make sure the escrow option is only sensitive if we're active, and
         * the escrow account is enabled  */
        gtk_widget_set_sensitive( GTK_WIDGET(escrowCb),
                                  gtk_toggle_button_get_active(tb)
                                  && gtk_toggle_button_get_active(
                                          GTK_TOGGLE_BUTTON(rouid->ldd->optEscrowCb)) );
}

static
void
ld_escrow_toggled( GtkToggleButton *tb, gpointer ud )
{
        RepayOptUIData *rouid;

        rouid = (RepayOptUIData*)ud;
        rouid->optData->throughEscrowP = gtk_toggle_button_get_active( tb );
}

static
gboolean
ld_info_save( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        LoanDruidData *ldd;

        ldd = (LoanDruidData*)ud;

        ldd->ld.primaryAcct = gnc_account_sel_get_account( ldd->prmAccountGAS );
        if ( ldd->ld.primaryAcct == NULL ) {
                gnc_info_dialog( ldd->dialog,
                                 _("Please select a valid loan account.") );
                return TRUE;
        } 
        if ( ! ldd->ld.repPriAcct ) {
                ldd->ld.repPriAcct = ldd->ld.primaryAcct;
        }
        ldd->ld.principal = gnc_amount_edit_get_amount( ldd->prmOrigPrincGAE );
        ldd->ld.interestRate =
                gtk_spin_button_get_value_as_float( ldd->prmIrateSpin );
        ldd->ld.type = gnc_option_menu_get_active( GTK_WIDGET(ldd->prmType) );
        if ( ldd->ld.type != FIXED ) {
                gnc_frequency_save_state( ldd->prmVarGncFreq,
                                          ldd->ld.loanFreq,
                                          ldd->ld.varStartDate );
        }

        /* start date */
        {
                time_t tmpTT;
                struct tm *tmpTm;

                tmpTT = gnc_date_edit_get_date( ldd->prmStartDateGDE );
                tmpTm = localtime( &tmpTT );
                g_date_set_dmy( ldd->ld.startDate,
                                tmpTm->tm_mday,
                                (tmpTm->tm_mon+1),
                                (1900 + tmpTm->tm_year) );
        }

        /* len / periods */
        {
                ldd->ld.perSize =
                        (gnc_option_menu_get_active( GTK_WIDGET(ldd->prmLengthType) )
                         == MONTHS) ? MONTHS : YEARS;
                ldd->ld.numPer =
                        gtk_spin_button_get_value_as_int( ldd->prmLengthSpin );
                ldd->ld.numMonRemain =
                        gtk_spin_button_get_value_as_int( ldd->prmRemainSpin );
        }
        return FALSE;
}

static
void
ld_info_prep( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        LoanDruidData *ldd;

        ldd = (LoanDruidData*)ud;
        gnc_amount_edit_set_amount( ldd->prmOrigPrincGAE, ldd->ld.principal );
        gtk_spin_button_set_value( ldd->prmIrateSpin, ldd->ld.interestRate );
        gtk_option_menu_set_history( ldd->prmType, ldd->ld.type );
        if ( ldd->ld.type != FIXED ) {
                gnc_frequency_setup( ldd->prmVarGncFreq,
                                     ldd->ld.loanFreq,
                                     ldd->ld.varStartDate );
        }

        /* start date */
        {
                struct tm *tmpTm;

                tmpTm = g_new0( struct tm, 1 );

                g_date_to_struct_tm( ldd->ld.startDate, tmpTm );
                gnc_date_edit_set_time( ldd->prmStartDateGDE,
                                        mktime(tmpTm) );
                g_free( tmpTm );
        }

        /* length: total and remaining */
        {
                gtk_spin_button_set_value( ldd->prmLengthSpin, ldd->ld.numPer );
                gtk_option_menu_set_history( ldd->prmLengthType, ldd->ld.perSize );
                gtk_spin_button_set_value( ldd->prmRemainSpin, ldd->ld.numMonRemain );
        }
}

static
gboolean
ld_opts_save_state( LoanDruidData *ldd )
{
        if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(ldd->optEscrowCb) ) ) {
                ldd->ld.escrowAcct =
                        gnc_account_sel_get_account( ldd->optEscrowGAS );
                if ( ldd->ld.escrowAcct == NULL ) {
                        gnc_info_dialog( ldd->dialog,
                                         _("Please select a valid "
                                           "Escrow Account.") );
                        return TRUE;
                }
                
        } else {
                ldd->ld.escrowAcct = NULL;
        }
        return FALSE;
}

static
gboolean
ld_opts_tran( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        return ld_opts_save_state( (LoanDruidData*)ud );
}

static
void
ld_opts_prep( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        int i;
        RepayOptUIData *rouid;
        LoanDruidData *ldd;

        ldd = (LoanDruidData*)ud;
        if ( ldd->ld.escrowAcct ) {
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(ldd->optEscrowCb),
                                              TRUE );
                gnc_account_sel_set_account( ldd->optEscrowGAS, ldd->ld.escrowAcct );
        }
        for ( i=0; i<ldd->ld.repayOptCount; i++ ) {
                rouid = ldd->repayOptsUI[i];
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(rouid->optCb),
                                              rouid->optData->enabled );
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(rouid->escrowCb),
                                              rouid->optData->throughEscrowP
                                              && rouid->optData->enabled
                                              && ldd->ld.escrowAcct );
                gtk_widget_set_sensitive( GTK_WIDGET(rouid->escrowCb),
                                          rouid->optData->enabled
                                          && ldd->ld.escrowAcct );
        }
}

static
gboolean
ld_rep_save( LoanDruidData *ldd )
{
        int i;

        if ( ldd->ld.repMemo )
                g_free( ldd->ld.repMemo );
        ldd->ld.repMemo =
                gtk_editable_get_chars( GTK_EDITABLE(ldd->repTxnName), 0, -1 );

        if ( ldd->ld.repAmount )
                g_free( ldd->ld.repAmount );
        ldd->ld.repAmount =
                gtk_editable_get_chars( GTK_EDITABLE(ldd->repAmtEntry), 0, -1 );

        ldd->ld.repFromAcct =
                gnc_account_sel_get_account( ldd->repAssetsFromGAS );
        if ( ldd->ld.repFromAcct == NULL ) {
                gnc_info_dialog( ldd->dialog,
                                 _("Please select a valid \"from\" account."));
                return TRUE;
        }
        ldd->ld.repPriAcct =
                gnc_account_sel_get_account( ldd->repPrincToGAS );
        if ( ldd->ld.repPriAcct == NULL ) {
                gnc_info_dialog( ldd->dialog,
                                 _("Please select a valid \"to\" account.") );
                return TRUE;
        }
        ldd->ld.repIntAcct =
                gnc_account_sel_get_account( ldd->repIntToGAS );
        if ( ldd->ld.repIntAcct == NULL ) {
                gnc_info_dialog( ldd->dialog,
                                 _("Please select a valid "
                                   "\"interest\" account.") );
                return TRUE;
        }
        gnc_frequency_save_state( ldd->repGncFreq,
                                  ldd->ld.repFreq,
                                  ldd->ld.repStartDate );

        /* Set the 'from' accounts of the various options to be the
         * Assets-From account, if they're not already something else. */
        for ( i=0; i<ldd->ld.repayOptCount; i++ ) {
                RepayOptData *rod = ldd->ld.repayOpts[i];
                if ( ! rod->from ) {
                        rod->from = ldd->ld.repFromAcct;
                }
        }
        return FALSE;
}

static
gboolean
ld_rep_next( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        LoanDruidData *ldd;

        ldd = (LoanDruidData*)ud;

        if ( ld_rep_save( ldd ) != FALSE ) {
                DEBUG( "Couldn't save, stopping here." );
                return TRUE;
        }

        {
                int i;
                for ( i = 0; // we can always start at 0, here.
                      (i < ldd->ld.repayOptCount)
                      && !ldd->ld.repayOpts[i]->enabled;
                      i++ )
                        ;
                if ( i < ldd->ld.repayOptCount ) {
                        ldd->currentIdx = i;
                        return FALSE;
                }
        }
        gnome_druid_set_page( ldd->druid,
                              GNOME_DRUID_PAGE(
                                      glade_xml_get_widget( ldd->gxml,
                                                            PG_REVIEW ) ) );
        return TRUE;
}

static
gboolean
ld_rep_back( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        LoanDruidData *ldd;

        ldd = (LoanDruidData*)ud;
        return ld_rep_save(ldd);
}

static
void
ld_rep_prep( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        LoanDruidData *ldd;
        GString *str;

        ldd = (LoanDruidData*)ud;

        if ( ldd->ld.repAmount ) {
                g_free( ldd->ld.repAmount );
        }

        str = g_string_sized_new( 64 );
        ld_get_pmt_formula( ldd, str );
        ldd->ld.repAmount = str->str;
        g_string_free( str, FALSE );

        if ( ldd->ld.repMemo )
                gtk_entry_set_text( ldd->repTxnName, ldd->ld.repMemo );

        if ( ldd->ld.repAmount )
                gtk_entry_set_text( ldd->repAmtEntry, ldd->ld.repAmount );

        gnc_account_sel_set_account( ldd->repAssetsFromGAS,
                                     ldd->ld.repFromAcct );
        gnc_account_sel_set_account( ldd->repPrincToGAS,
                                     ldd->ld.repPriAcct );
        gnc_account_sel_set_account( ldd->repIntToGAS,
                                     ldd->ld.repIntAcct );
        gnc_frequency_setup( ldd->repGncFreq,
                             ldd->ld.repFreq,
                             ldd->ld.repStartDate );
}

static
void
ld_pay_prep( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        LoanDruidData *ldd;
        RepayOptData *rod;
        GString *str;
        gboolean uniq;

        ldd = (LoanDruidData*)ud;
        g_assert( ldd->currentIdx >= 0 );
        g_assert( ldd->currentIdx <= ldd->ld.repayOptCount );

        rod = ldd->ld.repayOpts[ldd->currentIdx];
        str = g_string_sized_new( 32 );
        /* Translators: %s is "Taxes", or "Insurance", or similar */
        g_string_printf( str, _("Payment: \"%s\""), rod->name );
        gnome_druid_page_standard_set_title( GNOME_DRUID_PAGE_STANDARD(gdp),
                                             str->str );
        /* copy in the relevant data from the currently-indexed
         * option. */
        gtk_entry_set_text( ldd->payTxnName, rod->txnMemo );
        g_string_printf( str, "%0.2f", rod->amount );
        gtk_entry_set_text( ldd->payAmtEntry, str->str );

        gtk_widget_set_sensitive( GTK_WIDGET(ldd->payUseEscrow),
                                  (ldd->ld.escrowAcct != NULL) );

        {
                gtk_signal_handler_block_by_func( GTK_OBJECT(ldd->payUseEscrow),
                                                  GTK_SIGNAL_FUNC(ld_pay_use_esc_toggle),
                                                  ldd );

                ld_pay_use_esc_setup( ldd,
                                      (ldd->ld.escrowAcct != NULL)
                                      && rod->throughEscrowP );
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(ldd->payUseEscrow),
                                              (rod->throughEscrowP
                                               && ldd->ld.escrowAcct != NULL) );

                gtk_signal_handler_unblock_by_func( GTK_OBJECT(ldd->payUseEscrow),
                                                    GTK_SIGNAL_FUNC(ld_pay_use_esc_toggle),
                                                    ldd );
        }

        {
                gtk_signal_handler_block_by_func( GTK_OBJECT(ldd->paySpecSrcAcct),
                                                  GTK_SIGNAL_FUNC(ld_pay_spec_src_toggle),
                                                  ldd );

                ld_pay_spec_src_setup( ldd, rod->specSrcAcctP );
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(ldd->paySpecSrcAcct),
                                              rod->specSrcAcctP );

                gtk_signal_handler_unblock_by_func( GTK_OBJECT(ldd->paySpecSrcAcct),
                                                    GTK_SIGNAL_FUNC(ld_pay_spec_src_toggle),
                                                    ldd );
        }

        gnc_account_sel_set_account( ldd->payAcctToGAS,   rod->to );

        uniq = (rod->fs != NULL);
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(ldd->payTxnFreqPartRb),
                                      !uniq );
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(ldd->payTxnFreqUniqRb),
                                      uniq );
        gtk_widget_set_sensitive( GTK_WIDGET(ldd->payFreqAlign),
                                  uniq );
        if ( uniq ) {
                gnc_frequency_setup( ldd->payGncFreq,
                                     rod->fs, rod->startDate );
        }

        g_string_free( str, TRUE );
}

static
gboolean
ld_pay_save_current( LoanDruidData *ldd )
{
        gchar *tmpStr;
        RepayOptData *rod;

        g_assert( ldd->currentIdx >= 0 );
        g_assert( ldd->currentIdx <= ldd->ld.repayOptCount );
        rod = ldd->ld.repayOpts[ ldd->currentIdx ];

        tmpStr = gtk_editable_get_chars( GTK_EDITABLE(ldd->payTxnName),
                                         0, -1 );
        if ( rod->txnMemo != NULL ) {
                g_free( rod->txnMemo );
        }
        rod->txnMemo = tmpStr;
        tmpStr = NULL;

        tmpStr = gtk_editable_get_chars( GTK_EDITABLE(ldd->payAmtEntry),
                                         0, -1 );
        rod->amount = (float)strtod( tmpStr, NULL );
        g_free( tmpStr );

        rod->specSrcAcctP =
                gtk_toggle_button_get_active(
                        GTK_TOGGLE_BUTTON(ldd->paySpecSrcAcct) );

        rod->from = NULL;
        if ( rod->specSrcAcctP ) {
                rod->from = gnc_account_sel_get_account( ldd->payAcctFromGAS );
                if ( rod->from == NULL ) {
                        gnc_info_dialog( ldd->dialog,
                                         _("Please select a valid "
                                           "\"from\" account.") );
                        return TRUE;
                }
        }

        rod->to   = gnc_account_sel_get_account( ldd->payAcctToGAS );
        if ( rod->to == NULL ) {
                gnc_info_dialog( ldd->dialog,
                                 _("Please select a valid "
                                   "\"to\" account.") );
                return TRUE;
        }
        
        /* if ( rb toggled )
         *   ensure freqspec/startdate setup
         *   save
         * else
         *   if (freqspec setup)
         *     remove freqspec/startdate
         */

        /* neither of these should happen. */
        g_assert( ! (rod->fs && !rod->startDate) );
        g_assert( ! (!rod->fs && rod->startDate) );

        if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ldd->payTxnFreqUniqRb)) ) {
                if ( rod->fs == NULL ) {
                        rod->fs = xaccFreqSpecMalloc( gnc_get_current_book() );
                }
                if ( rod->startDate == NULL ) {
                        rod->startDate = g_date_new();
                }
                gnc_frequency_save_state( ldd->payGncFreq,
                                          rod->fs, rod->startDate );
        } else {
                if ( rod->fs ) {
                        xaccFreqSpecFree( rod->fs );
                        rod->fs = NULL;
                }
                if ( rod->startDate ) {
                        g_date_free( rod->startDate );
                        rod->startDate = NULL;
                }
        }
        return FALSE;
}

static
gboolean
ld_pay_next( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        int i;
        LoanDruidData *ldd;

        ldd = (LoanDruidData*)ud;
        /* save current data */
        if ( ld_pay_save_current( ldd ) != FALSE ) {
                return TRUE;
        }

        /* Go through opts list and select next enabled option. */
        for ( i = ldd->currentIdx + 1;
              (i < ldd->ld.repayOptCount)
              && !ldd->ld.repayOpts[i]->enabled; i++ )
                ;

        if ( i < ldd->ld.repayOptCount ) {
                ldd->currentIdx = i;
                ld_pay_prep( gdp, arg1, ud );
                return TRUE;
        }

        /* If there's no repayment options enabled, then go immediately to
         * the review page. */
        gnome_druid_set_page( ldd->druid,
                              GNOME_DRUID_PAGE(
                                      glade_xml_get_widget( ldd->gxml,
                                                            PG_REVIEW ) ) );
        return TRUE;
}

static
gboolean
ld_pay_back( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        int i;
        LoanDruidData *ldd;

        ldd = (LoanDruidData*)ud;

        /* save current data */
        if ( ld_pay_save_current( ldd ) != FALSE ) {
                return TRUE;
        }

        /* go back through opts list and select next enabled options. */
        for ( i = ldd->currentIdx - 1;
              (i > -1) && !ldd->ld.repayOpts[i]->enabled;
              i-- )
                ;
        if ( i >= 0 ) {
                ldd->currentIdx = i;
                ld_pay_prep( gdp, arg1, ud );
                return TRUE;
        }
        ldd->currentIdx = -1;
        return FALSE;
}

static
void
ld_pay_freq_toggle( GtkToggleButton *tb, gpointer ud )
{
        LoanDruidData *ldd;
        gboolean uniq;

        ldd = (LoanDruidData*)ud;

        g_assert( ldd->currentIdx >= 0 );
        g_assert( ldd->currentIdx <= ldd->ld.repayOptCount );
                  
        uniq = gtk_toggle_button_get_active(
                GTK_TOGGLE_BUTTON(ldd->payTxnFreqUniqRb) );
        gtk_widget_set_sensitive( GTK_WIDGET(ldd->payFreqAlign), uniq );

        if ( uniq ) {
                RepayOptData *rod;
                rod = ldd->ld.repayOpts[ ldd->currentIdx ];

                if ( rod->fs == NULL ) {
                        rod->fs = xaccFreqSpecMalloc( gnc_get_current_book() );
                        xaccFreqSpecSetMonthly( rod->fs, ldd->ld.startDate, 1 );
                        xaccFreqSpecSetUIType( rod->fs, UIFREQ_MONTHLY );
                }
                if ( rod->startDate == NULL ) {
                        rod->startDate = g_date_new();
                        *rod->startDate = *ldd->ld.startDate;
                }
                gnc_frequency_setup( ldd->payGncFreq,
                                     rod->fs, rod->startDate );
        }
}

static
void
ld_pay_use_esc_setup( LoanDruidData *ldd, gboolean newState )
{
        gtk_widget_set_sensitive( GTK_WIDGET(ldd->payEscToLabel), newState );
        gtk_widget_set_sensitive( GTK_WIDGET(ldd->payEscFromLabel), newState );
        if ( newState )
        {
                gnc_account_sel_set_account( ldd->payAcctEscToGAS,
                                             ldd->ld.escrowAcct );
                gnc_account_sel_set_account( ldd->payAcctEscFromGAS,
                                             ldd->ld.escrowAcct );
        }
}

static
void
ld_pay_use_esc_toggle( GtkToggleButton *tb, gpointer ud )
{
        gboolean newState;
        LoanDruidData *ldd = (LoanDruidData*)ud;

        newState = gtk_toggle_button_get_active( tb );
        ld_pay_use_esc_setup( ldd, newState );
}

static
void
ld_pay_spec_src_setup( LoanDruidData *ldd, gboolean newState )
{
        gtk_widget_set_sensitive( GTK_WIDGET(ldd->payAcctFromLabel), newState );
        gtk_widget_set_sensitive( GTK_WIDGET(ldd->payAcctFromGAS), newState );
        if ( newState )
        {
                gnc_account_sel_set_account( ldd->payAcctFromGAS,
                                             ldd->ld.repayOpts[ldd->currentIdx]
                                             ->from );
        }
}

static
void
ld_pay_spec_src_toggle( GtkToggleButton *tb, gpointer ud )
{
        gboolean newState;
        LoanDruidData *ldd = (LoanDruidData*)ud;

        newState = gtk_toggle_button_get_active( tb );
        ld_pay_spec_src_setup( ldd, newState );
}

static
gboolean
ld_rev_next( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        LoanDruidData *ldd;

        ldd = (LoanDruidData*)ud;
        gnome_druid_set_page( ldd->druid,
                              GNOME_DRUID_PAGE(
                                      glade_xml_get_widget( ldd->gxml,
                                                            PG_COMMIT ) ) );
        return TRUE;
}

static
void
ld_rev_prep( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        /* 3, here, does not include the Date column. */
        const static int BASE_COLS = 3;
        LoanDruidData *ldd;
        gchar **titles;
        int i;

        ldd = (LoanDruidData*)ud;

        /* Cleanup old clist */
        if ( ldd->revCL != NULL ) {
                gtk_container_remove( GTK_CONTAINER(ldd->revScrollWin),
                                      GTK_WIDGET(ldd->revCL) );
                ldd->revCL = NULL;
        }

        ldd->ld.revNumPmts = BASE_COLS;
        /* Get the correct number of repayment columns. */
        for ( i=0; i < ldd->ld.repayOptCount; i++ ) {
                ldd->ld.revRepayOptToColMap[i] = -1;
                if ( ! ldd->ld.repayOpts[i]->enabled ) {
                        continue;
                }
                /* not '+1' = there is no date column to be accounted for in
                 * the mapping. */
                ldd->ld.revRepayOptToColMap[i] = ldd->ld.revNumPmts;
                ldd->ld.revNumPmts += 1;
        }

        /* '+1' for leading date col */
        titles = g_new0( gchar*, ldd->ld.revNumPmts + 1 );
        titles[0] = _( "Date" );
        titles[1] = _( "Payment" );
        titles[2] = _( "Principal" );
        titles[3] = _( "Interest" );
        /* move the appropriate names over into the title array */
        {
                for ( i=0; i < ldd->ld.repayOptCount; i++ ) {
                        if ( ldd->ld.revRepayOptToColMap[i] == -1 ) {
                                continue;
                        }
                        /* '+1' offset for the "Date" title */
                        titles[ ldd->ld.revRepayOptToColMap[i] + 1 ] =
                                ldd->ld.repayOpts[i]->name;
                }
        }

        ldd->revCL = GTK_CLIST(
                gtk_clist_new_with_titles( ldd->ld.revNumPmts+1,
                                           titles ) );
        g_free( titles );

        for( i=0; i < ldd->ld.revNumPmts+1; i++ ) {
                gtk_clist_set_column_resizeable( ldd->revCL, i, TRUE );
                
        }

        gtk_signal_connect( GTK_OBJECT(ldd->revCL), "size-allocate",
                            GTK_SIGNAL_FUNC(ld_rev_clist_allocate_col_widths),
                            (gpointer)ldd );

        gtk_container_add( GTK_CONTAINER(ldd->revScrollWin),
                           GTK_WIDGET(ldd->revCL) );
        gtk_widget_show_all( GTK_WIDGET(ldd->revCL) );

        ld_rev_recalc_schedule( ldd );

        {
                GDate start, end;
                g_date_clear( &start, 1 );
                g_date_clear( &end, 1 );
                ld_rev_get_dates( ldd, &start, &end );
                ld_rev_update_clist( ldd, &start, &end );
        }
}

static
gboolean
ld_rev_back( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        LoanDruidData *ldd = (LoanDruidData*)ud;
        int i;

        /* Get the correct page based on the repayment state. */
        /* go back through opts list and select next enabled options. */
        for ( i = ldd->currentIdx;
              (i > -1) && !ldd->ld.repayOpts[i]->enabled;
              i-- )
                ;
        if ( i >= 0 ) {
                ldd->currentIdx = i;
                /* natural transition to the payments page */
                return FALSE;
        }

        /* If there are no payment options, then go directly to the main
         * repayment page. */
        gnome_druid_set_page( ldd->druid,
                              GNOME_DRUID_PAGE(
                                      glade_xml_get_widget( ldd->gxml,
                                                            PG_REPAYMENT ) ) );
        return TRUE;
}

static
gboolean
ld_com_next( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        g_assert( FALSE );
        return TRUE;
}

static
void
ld_com_prep( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
}

static
gboolean
ld_com_back( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        LoanDruidData *ldd = (LoanDruidData*)ud;
        gnome_druid_set_page( ldd->druid,
                              GNOME_DRUID_PAGE(
                                      glade_xml_get_widget( ldd->gxml,
                                                            PG_REVIEW ) ) );
        return TRUE;
}

static
void
ld_com_fin( GnomeDruidPage *gdp, gpointer arg1, gpointer ud )
{
        LoanDruidData *ldd = (LoanDruidData*)ud;
        ld_create_sxes( ldd );
        ld_close_handler( ldd );

}

#if 0
static
void
ld_gnc_ttinfo_free( gpointer data, gpointer ud )
{
        gnc_ttinfo_free( (TTInfo*)data );
}
#endif

static
int
ld_calc_current_instance_num( int monthsPassed, FreqSpec *fs )
{
        float mult = 1.0;
        UIFreqType uift;
        uift = xaccFreqSpecGetUIType( fs );
        switch ( uift ) {
        case UIFREQ_WEEKLY:
        {
                int wMult, dow;
                xaccFreqSpecGetWeekly( fs, &wMult, &dow );
                mult = (4.0 / wMult);
        }
        break;
        case UIFREQ_BI_WEEKLY:
        case UIFREQ_SEMI_MONTHLY:
                mult = 2.0;
                break;
        case UIFREQ_MONTHLY:
        case UIFREQ_QUARTERLY:
        case UIFREQ_TRI_ANUALLY:
        case UIFREQ_SEMI_YEARLY:
        case UIFREQ_YEARLY:
        {
                int mMult, dom, offset;
                xaccFreqSpecGetMonthly( fs, &mMult, &dom, &offset );
                mult = ( 1.0 / mMult );
        }
        break;
        default:
                PERR( "Wacky loan repayment frequency [%d]", uift );
                break;
        }

        return floor( monthsPassed * mult );
}

static
void
ld_tcSX_free( gpointer data, gpointer user_data )
{
        toCreateSX *tcSX = (toCreateSX*)data;
        g_free( tcSX->name );
        if ( tcSX->mainTxn )
                gnc_ttinfo_free( tcSX->mainTxn );
        if ( tcSX->escrowTxn )
                gnc_ttinfo_free( tcSX->escrowTxn );
        g_free( tcSX );
}

/**
 * Custom GCompareFunc to find the element of a GList of TTSplitInfo's which
 * has the given [Account*] criteria.
 * @return 0 if match, as per GCompareFunc in the g_list_find_custom context.
 **/
static
gint
ld_find_ttsplit_with_acct( gconstpointer elt,
                           gconstpointer crit )
{
        TTSplitInfo *ttsi = (TTSplitInfo*)elt;
        return ( (gnc_ttsplitinfo_get_account( ttsi )
                  == (Account*)crit) ? 0 : 1 );
}

/**
 * Enters into the books a Scheduled Transaction from the given toCreateSX.
 **/
static
void
ld_create_sx_from_tcSX( LoanDruidData *ldd, toCreateSX *tcSX )
{
        SchedXaction *sx;
        GList *ttxnList, *sxList;

        sx = xaccSchedXactionMalloc( gnc_get_current_book() );
        xaccSchedXactionSetName( sx, tcSX->name );
        xaccSchedXactionSetFreqSpec( sx, tcSX->freq );
        xaccSchedXactionSetStartDate( sx, &tcSX->start );
        xaccSchedXactionSetLastOccurDate( sx, &tcSX->last );
        xaccSchedXactionSetEndDate( sx, &tcSX->end );
        gnc_sx_set_instance_count( sx, tcSX->instNum );

        ttxnList = NULL;
        if ( tcSX->mainTxn )
                ttxnList = g_list_append( ttxnList, tcSX->mainTxn );
        if ( tcSX->escrowTxn )
                ttxnList = g_list_append( ttxnList, tcSX->escrowTxn );

        g_assert( ttxnList != NULL );

        xaccSchedXactionSetTemplateTrans( sx, ttxnList,
                                          gnc_get_current_book() );

        sxList = gnc_book_get_schedxactions( gnc_get_current_book() );
        sxList = g_list_append( sxList, sx );
        gnc_book_set_schedxactions( gnc_get_current_book(), sxList );

        g_list_free( ttxnList );
        ttxnList = NULL;
}

/**
 * Does the work to setup the given toCreateSX structure for a specific
 * repayment.  Note that if the RepayOptData doesn't specify a unique
 * FreqSpec, the paymentSX and the tcSX parameters will be the same.
 **/
static
void
ld_setup_repayment_sx( LoanDruidData *ldd,
                       RepayOptData *rod,
                       toCreateSX *paymentSX,
                       toCreateSX *tcSX )
{
        /* In DoubleEntryAccounting-ease, this is what we're going to do,
         * below...
         *
         * if ( rep->escrow ) {
         *   if ( rep->from ) {
         *      a: paymentSX.main.splits += split( rep->fromAcct, repAmt )
         *      b: paymentSX.main.split( ldd->ld.escrowAcct ).debCred += repAmt
         *         tcSX.escrow.split( rep->escrow ).debCred += repAmt
         *     c1: tcSX.escrow.splits += split( rep->toAcct, +repAmt )
         *   } else {
         *      d: paymentSX.main.split( ldd->ld.repFromAcct ).debcred += -repAmt
         *      b: paymentSX.main.split( ldd->ld.escrowAcct ).debCred += repAmt
         *         tcSX.escrow.splits += split( rep->escrow, -repAmt )
         *     c1: tcSX.escrow.splits += split( rep->toAcct, +repAmt )
         *   }
         * } else {
         *   if ( rep->from ) {
         *      a: paymentSX.main.splits += split( rep->fromAcct, -repAmt )
         *     c2: paymentSX.main.splits += split( rep->toAcct,   +repAmt )
         *   } else {
         *      d: paymentSX.main.split( ldd->ld.payFromAcct ).debcred += -repAmt
         *     c2: paymentSX.main.splits += split( rep->toAcct, +repAmt )
         *   }
         * }
         */

        /* Now, we refactor the common operations from the above out...
         *
         * fromSplit = NULL;
         * if ( rep->escrow ) {
         *   b: paymentSX.main.split( ldd->ld.escrowAcct ).debCred += repAmt
         *  c1: ( toTTI = tcSX.escrow )
         *   if ( rep->from ) {
         *     a1: (fromSplit = NULL) paymentSX.main.splits += split( rep->fromAcct, repAmt )
         *      b: 
         *         tcSX.escrow.split( rep->escrow ).debCred += repAmt
         *     c1:
         *   } else {
         *     a2: (fromSplit = paymentSX.main.split( ldd->ld.repFromAcct )) .debcred += -repAmt
         *      b: 
         *         tcSX.escrow.splits += split( rep->escrow, -repAmt )
         *     c1:
         *   }
         * } else {
         *   c2: ( toTTI = paymentSX.main )
         *   if ( rep->from ) {
         *     a1: (fromSplit = NULL) paymentSX.main.splits += split( rep->fromAcct, -repAmt )
         *     c2: 
         *   } else {
         *     a2: (fromSplit = paymentSX.main.split( ldd->ld.payFromAcct )).debcred += -repAmt
         *     c2: 
         *   }
         * }
         * if ( fromSplit ) {
         *   fromSplit.debCred += (-repAmt);
         * } else {
         *   fromSplit = split( rep->fromAcct, -repAmt )
         *   paymentSX.main.splits += fromSplit
         * }
         * toTTI.splits += split( rep->toAcct, +repAmt );
         */

        /** Now, the actual implementation... */

        GString *gstr;
        GList *elt;
        TTSplitInfo *fromSplit = NULL;
        TTSplitInfo *ttsi;
        TTInfo *toTxn = NULL;
        GNCPrintAmountInfo pricePAI = gnc_default_price_print_info();
#define AMTBUF_LEN 64
        gchar amtBuf[AMTBUF_LEN];
        gint GNCN_HOW = (GNC_DENOM_SIGFIGS(2) | GNC_RND_ROUND);

        /* We're going to use this a lot, below, so just create it once. */
        xaccSPrintAmount( amtBuf,
                          double_to_gnc_numeric( rod->amount, 100,
                                                  GNCN_HOW ),
                          pricePAI );

        if ( rod->throughEscrowP && ldd->ld.escrowAcct ) {
                toTxn = tcSX->escrowTxn;

                /* Add the repayment amount into the string of the existing
                 * ttsplit. */
                {
                        elt = g_list_find_custom(
                                gnc_ttinfo_get_template_splits( paymentSX->mainTxn ),
                                ldd->ld.escrowAcct,
                                ld_find_ttsplit_with_acct );
                        g_assert( elt );
                        ttsi = (TTSplitInfo*)elt->data;
                        g_assert( ttsi );
                        gstr = g_string_new( gnc_ttsplitinfo_get_debit_formula( ttsi ) );
                        g_string_append_printf( gstr, " + %s", amtBuf );
                        gnc_ttsplitinfo_set_debit_formula( ttsi, gstr->str );
                        g_string_free( gstr, TRUE );
                        gstr = NULL;
                        ttsi = NULL;
                }
                
                if ( rod->from != NULL ) {
                        gchar *str;

                        fromSplit = NULL;

                        /* tcSX.escrow.split( rep->escrow ).debCred += repAmt */
                        elt = g_list_find_custom(
                                gnc_ttinfo_get_template_splits( tcSX->escrowTxn ),
                                ldd->ld.escrowAcct,
                                ld_find_ttsplit_with_acct );
                        ttsi = NULL;
                        if ( elt ) {
                                ttsi = (TTSplitInfo*)elt->data;
                        }
                        if ( !ttsi ) {
                                /* create split */
                                ttsi = gnc_ttsplitinfo_malloc();
                                gnc_ttsplitinfo_set_memo( ttsi, rod->txnMemo );
                                gnc_ttsplitinfo_set_account( ttsi, ldd->ld.escrowAcct );
                                gnc_ttinfo_append_template_split( tcSX->escrowTxn, ttsi );
                        }
                        if ( (str = (gchar*)gnc_ttsplitinfo_get_credit_formula( ttsi ))
                             == NULL ) {
                                gstr = g_string_sized_new( 16 );
                        } else {
                                /* If we did get a split/didn't have to
                                 * create a split, then we need to add our
                                 * amount in rather than replace. */
                                gstr = g_string_new( str );
                                g_string_append_printf( gstr, " + " );
                        }
                        g_string_append_printf( gstr, "%s", amtBuf );
                        gnc_ttsplitinfo_set_credit_formula( ttsi, gstr->str );
                        g_string_free( gstr, TRUE );
                        gstr = NULL;
                        ttsi = NULL;
                } else {
                        /* (fromSplit = paymentSX.main.split( ldd->ld.repFromAcct )) */
                        elt = g_list_find_custom(
                                gnc_ttinfo_get_template_splits( paymentSX->mainTxn ),
                                ldd->ld.repFromAcct,
                                ld_find_ttsplit_with_acct );
                        g_assert( elt );
                        fromSplit = (TTSplitInfo*)elt->data;

                        /* tcSX.escrow.splits += split( rep->escrow, -repAmt ) */
                        ttsi = gnc_ttsplitinfo_malloc();
                        gnc_ttsplitinfo_set_memo( ttsi, rod->txnMemo );
                        gnc_ttsplitinfo_set_account( ttsi, ldd->ld.escrowAcct );
                        gnc_ttsplitinfo_set_credit_formula( ttsi, amtBuf );
                        gnc_ttinfo_append_template_split( tcSX->escrowTxn, ttsi );
                        ttsi = NULL;
                }
        } else {
                toTxn = tcSX->mainTxn;

                if ( rod->from != NULL ) {
                        fromSplit = NULL;
                } else {
                        /* (fromSplit = paymentSX.main.split( ldd->ld.repFromAcct )) */
                        elt = g_list_find_custom(
                                gnc_ttinfo_get_template_splits( tcSX->mainTxn ),
                                ldd->ld.repFromAcct,
                                ld_find_ttsplit_with_acct );
                        fromSplit = NULL;
                        if ( elt ) {
                                /* This is conditionally true in the case of
                                 * a repayment on it's own schedule. */
                                fromSplit = (TTSplitInfo*)elt->data;
                        }
                }
        }

        if ( fromSplit != NULL ) {
                /* Update the existing from-split. */
                gstr = g_string_new( gnc_ttsplitinfo_get_credit_formula( fromSplit ) );
                g_string_append_printf( gstr, " + %s", amtBuf );
                gnc_ttsplitinfo_set_credit_formula( fromSplit, gstr->str );
                g_string_free( gstr, TRUE );
                gstr = NULL;

        } else {
                TTInfo *tti;
                /* Create a new from-split. */
                ttsi = gnc_ttsplitinfo_malloc();
                gnc_ttsplitinfo_set_memo( ttsi, rod->txnMemo );
                if ( rod->from ) {
                        gnc_ttsplitinfo_set_account( ttsi, rod->from );
                } else {
                        gnc_ttsplitinfo_set_account( ttsi, ldd->ld.repFromAcct );
                }
                gnc_ttsplitinfo_set_credit_formula( ttsi, amtBuf );
                tti = tcSX->mainTxn;
                if ( rod->throughEscrowP ) {
                        tti = paymentSX->mainTxn;
                }
                gnc_ttinfo_append_template_split( tti, ttsi );
                ttsi = NULL;
                tti  = NULL;
        }
        
        /* Add to-account split. */
        {
                ttsi = gnc_ttsplitinfo_malloc();
                gnc_ttsplitinfo_set_memo( ttsi, rod->txnMemo );
                gnc_ttsplitinfo_set_account( ttsi, rod->to );
                gnc_ttsplitinfo_set_debit_formula( ttsi, amtBuf );
                gnc_ttinfo_append_template_split( toTxn, ttsi );
                ttsi = NULL;
        }
}

/**
 * Actually does the heavy-lifting of creating the SXes from the
 * LoanDruidData.
 *
 * Rules:
 * - There is at least one SX created, with at least one txn, for the loan
 *   payment itself.
 * - A new SX is created for each repayment with a different frequency.
 * - Non-unique repayment From-accounts cause a "summed (src-)split", unique
 *   repayment From-accounts cause new (src-)splits.
 * - Each repayment causes a new (dst-)split [the To-account].
 * - Escrow-diverted repayments cause new Txns w/in their
 *   SX. [Assets->Escrow, Escrow->(Expense|Liability)]
 **/
static
void
ld_create_sxes( LoanDruidData *ldd )
{
        /* The main loan-payment SX.*/
        toCreateSX *paymentSX = NULL;
        /* A GList of any other repayment SXes with different FreqSpecs. */
        GList *repaySXes = NULL;
        /* The currently-being-referenced toCreateSX. */
        toCreateSX *tcSX;
        int i;
        TTInfo *ttxn;
        TTSplitInfo *ttsi;
        GString *gstr;

        paymentSX = g_new0( toCreateSX, 1 );
        paymentSX->name  = g_strdup(ldd->ld.repMemo);
        paymentSX->start = *ldd->ld.startDate;
        paymentSX->last  = *ldd->ld.repStartDate;
        g_date_subtract_months( &paymentSX->last, 1 );
        {
                paymentSX->end = *ldd->ld.repStartDate;
                g_date_add_months( &paymentSX->end, ldd->ld.numMonRemain - 1);
        }
        paymentSX->freq = ldd->ld.repFreq;
        /* Figure out the correct current instance-count for the txns in the
         * SX. */
        paymentSX->instNum =
                (ldd->ld.numPer * ( ldd->ld.perSize == YEARS ? 12 : 1 ))
                - ldd->ld.numMonRemain + 1;

        paymentSX->mainTxn = gnc_ttinfo_malloc();
        gnc_ttinfo_set_currency( paymentSX->mainTxn,
                                 gnc_default_currency() );
        {
                GString *payMainTxnDesc = g_string_sized_new( 32 );
                g_string_printf( payMainTxnDesc,
                                 "%s - %s%s",
                                 ldd->ld.repMemo,
                                 ( ldd->ld.escrowAcct == NULL
                                   ? "" : _("Escrow ") ),
                                 _("Payment") );
                
                gnc_ttinfo_set_description( paymentSX->mainTxn,
                                            payMainTxnDesc->str );
                g_string_free( payMainTxnDesc, TRUE );
        }

        /* Create the basic txns and splits...
         *
         * ttxn <- mainTxn
         * srcAcct <- assets
         * if ( escrow ) {
         *  realSrcAcct <- srcAcct
         *  srcAcct     <- escrow;
         *  ttxn <- escrowTxn
         *  main.splits += split( realSrcAcct, -pmt )
         *  main.splits += split( escrow,       pmt )
         * }
         * ttxn.splits += split( escrow,            -pmt)
         * ttxn.splits += split( liability,          ppmt )
         * ttxn.splits += split( expenses:interest,  ipmt ) */
        
        {
                Account *srcAcct;

                ttxn = paymentSX->mainTxn;
                srcAcct = ldd->ld.repFromAcct;

                if ( ldd->ld.escrowAcct != NULL ) {
                        Account *realSrcAcct = srcAcct;
                        srcAcct = ldd->ld.escrowAcct;

                        gstr = g_string_sized_new( 32 );
                        ld_get_pmt_formula( ldd, gstr );
                        /* ttxn.splits += split( realSrcAcct, -pmt ); */
                        {
                                ttsi = gnc_ttsplitinfo_malloc();
                                gnc_ttsplitinfo_set_memo( ttsi, ldd->ld.repMemo );
                                gnc_ttsplitinfo_set_account( ttsi, realSrcAcct );
                                gnc_ttsplitinfo_set_credit_formula( ttsi, gstr->str );
                                gnc_ttinfo_append_template_split( ttxn, ttsi );
                                ttsi = NULL;
                        }

                        /* ttxn.splits += split( escrowAcct, +pmt ); */
                        {
                                ttsi = gnc_ttsplitinfo_malloc();
                                gnc_ttsplitinfo_set_memo( ttsi, ldd->ld.repMemo );
                                gnc_ttsplitinfo_set_account( ttsi, ldd->ld.escrowAcct );
                                gnc_ttsplitinfo_set_debit_formula( ttsi, gstr->str );
                                gnc_ttinfo_append_template_split( ttxn, ttsi );
                                ttsi = NULL;
                        }
                        g_string_free( gstr, TRUE );
                        gstr = NULL;

                        paymentSX->escrowTxn = gnc_ttinfo_malloc();
                        gnc_ttinfo_set_currency( paymentSX->escrowTxn,
                                                 gnc_default_currency() );

                        {
                                GString *escrowTxnDesc;
                                escrowTxnDesc = g_string_new( ldd->ld.repMemo );
                                g_string_append_printf( escrowTxnDesc, " - %s", _("Payment") );
                                gnc_ttinfo_set_description( paymentSX->escrowTxn,
                                                            escrowTxnDesc->str );
                                g_string_free( escrowTxnDesc, TRUE );
                        }
                        ttxn = paymentSX->escrowTxn;
                }

                /* ttxn.splits += split( srcAcct, -pmt ); */
                {
                        ttsi = gnc_ttsplitinfo_malloc();
                        {
                                gstr = g_string_new( ldd->ld.repMemo );
                                g_string_append_printf( gstr, " - %s",
                                                        _("Payment") );
                                gnc_ttsplitinfo_set_memo( ttsi, gstr->str );
                                g_string_free( gstr, TRUE );
                                gstr = NULL;
                        }
                        gnc_ttsplitinfo_set_account( ttsi, srcAcct );
                        gstr = g_string_sized_new( 32 );
                        ld_get_pmt_formula( ldd, gstr );
                        gnc_ttsplitinfo_set_credit_formula( ttsi, gstr->str );
                        gnc_ttinfo_append_template_split( ttxn, ttsi );
                        g_string_free( gstr, TRUE );
                        gstr = NULL;
                        ttsi = NULL;
                }

                /* ttxn.splits += split( ldd->ld.repPriAcct, +ppmt ); */
                {
                        ttsi = gnc_ttsplitinfo_malloc();
                        {
                                gstr = g_string_new( ldd->ld.repMemo );
                                g_string_append_printf( gstr, " - %s",
                                                        _("Principal") );
                                gnc_ttsplitinfo_set_memo( ttsi, gstr->str );
                                g_string_free( gstr, TRUE );
                                gstr = NULL;
                        }
                        gnc_ttsplitinfo_set_account( ttsi, ldd->ld.repPriAcct );
                        gstr = g_string_sized_new( 32 );
                        ld_get_ppmt_formula( ldd, gstr );
                        gnc_ttsplitinfo_set_debit_formula( ttsi, gstr->str );
                        gnc_ttinfo_append_template_split( ttxn, ttsi );
                        g_string_free( gstr, TRUE );
                        gstr = NULL;
                        ttsi = NULL;
                }

                /* ttxn.splits += split( ldd->ld.repIntAcct, +ipmt ); */
                {
                        ttsi = gnc_ttsplitinfo_malloc();
                        {
                                gstr = g_string_new( ldd->ld.repMemo );
                                g_string_append_printf( gstr, " - %s",
                                                        _("Interest") );
                                gnc_ttsplitinfo_set_memo( ttsi, gstr->str );
                                g_string_free( gstr, TRUE );
                                gstr = NULL;
                        }
                        gnc_ttsplitinfo_set_account( ttsi, ldd->ld.repIntAcct );
                        gstr = g_string_sized_new( 32 );
                        ld_get_ipmt_formula( ldd, gstr );
                        gnc_ttsplitinfo_set_debit_formula( ttsi, gstr->str );
                        gnc_ttinfo_append_template_split( ttxn, ttsi );
                        g_string_free( gstr, TRUE );
                        gstr = NULL;
                        ttsi = NULL;
                }
        }

        for ( i=0; i < ldd->ld.repayOptCount; i++ ) {
                RepayOptData *rod = ldd->ld.repayOpts[i];
                if ( ! rod->enabled )
                        continue;

                tcSX = paymentSX;
                if ( rod->fs != NULL ) {
                        tcSX = g_new0( toCreateSX, 1 );
                        gstr = g_string_new( ldd->ld.repMemo );
                        g_string_append_printf( gstr, " - %s",
                                                rod->name );
                        tcSX->name    = g_strdup(gstr->str);
                        tcSX->start   = *ldd->ld.startDate;
                        tcSX->last    = *ldd->ld.repStartDate;
                        {
                                tcSX->end = tcSX->last;
                                g_date_add_months( &tcSX->end, ldd->ld.numMonRemain );
                        }
                        tcSX->freq    = rod->fs;
                        /* So it won't get destroyed when the close the
                         * Druid. */
                        tcSX->instNum = ld_calc_current_instance_num( paymentSX->instNum,
                                                                      rod->fs );
                        rod->fs       = NULL;
                        tcSX->mainTxn   = gnc_ttinfo_malloc();
                        gnc_ttinfo_set_currency( tcSX->mainTxn,
                                                 gnc_default_currency() );
                        gnc_ttinfo_set_description( tcSX->mainTxn,
                                                    gstr->str );
                        tcSX->escrowTxn = gnc_ttinfo_malloc();
                        gnc_ttinfo_set_currency( tcSX->escrowTxn,
                                                 gnc_default_currency() );
                        gnc_ttinfo_set_description( tcSX->escrowTxn,
                                                    gstr->str );
                        
                        g_string_free( gstr, TRUE );
                        gstr = NULL;

                        repaySXes = g_list_append( repaySXes, tcSX );

                }

                /* repayment */
                ld_setup_repayment_sx( ldd, rod, paymentSX, tcSX );
        }

        /* Create the SXes */
        {
                GList *l;

                ld_create_sx_from_tcSX( ldd, paymentSX );

                for ( l=repaySXes; l; l = l->next ) {
                        ld_create_sx_from_tcSX( ldd, (toCreateSX*)l->data );
                }
        }

        /* Clean up. */
        ld_tcSX_free( paymentSX, NULL );
        g_list_foreach( repaySXes, ld_tcSX_free, NULL );
        g_list_free( repaySXes );
}

static
void
ld_calc_upd_rem_payments( GtkWidget *w, gpointer ud )
{
        LoanDruidData *ldd = (LoanDruidData*)ud;
        GDate start, now;
        int i, totalVal, total, remain;

        g_date_clear( &start, 1 );
        g_date_clear( &now, 1 );
        g_date_set_time( &start, gnc_date_edit_get_date( ldd->prmStartDateGDE ) );
        g_date_set_time( &now, time(NULL) );
        for ( i=0; g_date_compare( &start, &now ) < 0; i++ ) {
                g_date_add_months( &start, 1 );
        }

        /* Get the correct, current value of the length spin. */
        {
                gchar *valueStr = gtk_editable_get_chars( GTK_EDITABLE(ldd->prmLengthSpin),
                                                          0, -1 );
                totalVal = strtol( valueStr, NULL, 10 );
                g_free( valueStr );
        }
        total = totalVal
                * ( gnc_option_menu_get_active(
                            GTK_WIDGET(ldd->prmLengthType) )
                    == 1 ? 12 : 1 );
        remain = total - i;
        gtk_spin_button_set_value( ldd->prmRemainSpin, remain );
        gtk_widget_show( GTK_WIDGET(ldd->prmRemainSpin) );
}

static
void
ld_rev_range_opt_changed( GtkButton *b, gpointer ud )
{
        LoanDruidData *ldd = (LoanDruidData*)ud;
        int opt;

        opt = gnc_option_menu_get_active( GTK_WIDGET(ldd->revRangeOpt) );
        gtk_widget_set_sensitive( GTK_WIDGET(ldd->revDateFrame),
                                  (opt == CUSTOM) );
        {
                GDate start, end;
                g_date_clear( &start, 1 );
                g_date_clear( &end, 1 );
                ld_rev_get_dates( ldd, &start, &end );
                ld_rev_update_clist( ldd, &start, &end );
        }
}

static
void
ld_rev_range_changed( GNCDateEdit *gde, gpointer ud )
{
        LoanDruidData *ldd = (LoanDruidData*)ud;
        {
                GDate start, end;
                g_date_clear( &start, 1 );
                g_date_clear( &end, 1 );
                ld_rev_get_dates( ldd, &start, &end );
                ld_rev_update_clist( ldd, &start, &end );
        }
}

static
void
ld_get_loan_range( LoanDruidData *ldd, GDate *start, GDate *end )
{
        int monthsTotal;
        struct tm *endDateMath;

        *start = *ldd->ld.startDate;

        endDateMath = g_new0( struct tm, 1 );
        g_date_to_struct_tm( ldd->ld.startDate, endDateMath );
        monthsTotal = ( (ldd->ld.numPer - 1)
                        * ( ldd->ld.perSize == MONTHS ? 1 : 12 ) );
        endDateMath->tm_mon += monthsTotal;
        g_date_set_time( end, mktime( endDateMath ) );
        g_free( endDateMath );
}

static
void
ld_rev_get_dates( LoanDruidData *ldd, GDate *start, GDate *end )
{
        int range = gnc_option_menu_get_active( GTK_WIDGET(ldd->revRangeOpt) );
        switch ( range ) {
        case CURRENT_YEAR:
                g_date_set_time( start, time(NULL) );
                g_date_set_dmy( start, 1, G_DATE_JANUARY, g_date_get_year( start ) );
                g_date_set_dmy( end, 31, G_DATE_DECEMBER, g_date_get_year( start ) );
                break;
        case NOW_PLUS_ONE:
                g_date_set_time( start, time(NULL) );
                *end = *start;
                g_date_add_years( end, 1 );
                break;
        case WHOLE_LOAN:
                ld_get_loan_range( ldd, start, end );
                break;
        case CUSTOM:
                g_date_set_time( start,
                                 gnc_date_edit_get_date( ldd->revStartDate ) );
                g_date_set_time( end,
                                 gnc_date_edit_get_date( ldd->revEndDate ) );
                break;
        default:
                PERR( "Unknown review date range option %d", range );
                break;
        }
       
}

static
void
ld_rev_sched_list_free( gpointer data, gpointer user_data )
{
        RevRepaymentRow *rrr = (RevRepaymentRow*)data;
        g_free( rrr->numCells );
        g_free( rrr );
}

static
void
ld_rev_hash_to_list( gpointer key, gpointer val, gpointer user_data )
{
        GList **l = (GList**)user_data;
        RevRepaymentRow *rrr = g_new0( RevRepaymentRow, 1 );
        if ( !key || !val ) {
                DEBUG( "%.8x, %.8x",
                       GPOINTER_TO_UINT(key),
                       GPOINTER_TO_UINT(val));
                return;
        }
        rrr->date = *(GDate*)key;
        rrr->numCells = (gnc_numeric*)val;
        *l = g_list_append( *l, (gpointer)rrr );
}

static
void
ld_rev_hash_free_date_keys( gpointer key, gpointer val, gpointer user_data )
{
        g_free( (GDate*)key );
}

static
void
ld_rev_recalc_schedule( LoanDruidData *ldd )
{
        GDate start, end;
        gnc_numeric *rowNumData;
        GHashTable *schedule;

        g_date_clear( &start, 1 );
        g_date_clear( &end, 1 );
        ld_get_loan_range( ldd, &start, &end );

        /* The schedule is a hash of GDates to row-of-gnc_numeric[N] data,
         * where N is the number of columns as determined by the _prep
         * function, and stored in LoanData::revNumPmts. */
        schedule = g_hash_table_new( g_date_hash, g_date_equals );

        /* Do the master repayment */
        {
                GDate curDate;
                GString *pmtFormula, *ppmtFormula, *ipmtFormula;
                int i;
                GHashTable *ivar;
                
                pmtFormula = g_string_sized_new( 64 );
                ld_get_pmt_formula( ldd, pmtFormula );
                ppmtFormula = g_string_sized_new( 64 );
                ld_get_ppmt_formula( ldd, ppmtFormula );
                ipmtFormula = g_string_sized_new( 64 );
                ld_get_ipmt_formula( ldd, ipmtFormula );

                ivar = g_hash_table_new( g_str_hash, g_str_equal );
                g_date_clear( &curDate, 1 );
                curDate = start;
                g_date_subtract_days( &curDate, 1 );
                xaccFreqSpecGetNextInstance( ldd->ld.repFreq,
                                             &curDate, &curDate );
                for ( i=1;
                      g_date_valid( &curDate )
                      && g_date_compare( &curDate, &end ) <= 0 ;
                      i++,
                      xaccFreqSpecGetNextInstance( ldd->ld.repFreq,
                                                   &curDate, &curDate ) )
                {
                        gnc_numeric ival;
                        gnc_numeric val;
                        char *eloc;
                        rowNumData =
                                (gnc_numeric*)g_hash_table_lookup( schedule,
                                                                   &curDate );
                        if ( rowNumData == NULL) {
                                int j;
                                GDate *dateKeyCopy = g_date_new();

                                *dateKeyCopy = curDate;
                                rowNumData = g_new0( gnc_numeric, ldd->ld.revNumPmts );
                                g_assert( rowNumData != NULL );
                                for ( j=0; j<ldd->ld.revNumPmts; j++ ) {
                                        rowNumData[j] = gnc_numeric_error( GNC_ERROR_ARG );
                                }
                                g_hash_table_insert( schedule,
                                                     (gpointer)dateKeyCopy,
                                                     (gpointer)rowNumData );
                        }
                        
                        /* evaluate the expressions given the correct
                         * sequence number i */
                        ival = gnc_numeric_create( i, 1 );
                        g_hash_table_insert( ivar, "i", &ival );

                        if ( ! gnc_exp_parser_parse_separate_vars(
                                     pmtFormula->str, &val, &eloc, ivar ) ) {
                                PERR( "pmt Parsing error at %s", eloc );
                                continue;
                        }
                        val = gnc_numeric_convert( val, 100, GNC_RND_ROUND );
                        rowNumData[0] = val;

                        if ( ! gnc_exp_parser_parse_separate_vars(
                                     ppmtFormula->str, &val, &eloc, ivar ) ) {
                                PERR( "ppmt Parsing error at %s", eloc );
                                continue;
                        }
                        val = gnc_numeric_convert( val, 100, GNC_RND_ROUND );
                        rowNumData[1] = val;

                        if ( ! gnc_exp_parser_parse_separate_vars(
                                     ipmtFormula->str, &val, &eloc, ivar ) ) {
                                PERR( "ipmt Parsing error at %s", eloc );
                                continue;
                        }
                        val = gnc_numeric_convert( val, 100, GNC_RND_ROUND );
                        rowNumData[2] = val;
                }

                g_string_free( ipmtFormula, TRUE );
                g_string_free( ppmtFormula, TRUE );
                g_string_free( pmtFormula, TRUE );

                g_hash_table_destroy( ivar );
        }

        /* Process any other enabled payments. */
        {
                int i;
                GDate curDate;
                FreqSpec *fs;

                for ( i=0; i<ldd->ld.repayOptCount; i++ )
                {
                        if ( ! ldd->ld.repayOpts[i]->enabled )
                                continue;

                        fs = ( ldd->ld.repayOpts[i]->fs != NULL
                               ? ldd->ld.repayOpts[i]->fs
                               : ldd->ld.repFreq );

                        g_date_clear( &curDate, 1 );
                        curDate = start;
                        g_date_subtract_days( &curDate, 1 );
                        xaccFreqSpecGetNextInstance( fs, &curDate, &curDate );
                        for ( ; g_date_valid( &curDate )
                                && g_date_compare( &curDate, &end ) <= 0;
                              xaccFreqSpecGetNextInstance(
                                      fs, &curDate, &curDate ) )
                        {
                                gint gncn_how =
                                        GNC_DENOM_SIGFIGS(2)
                                        | GNC_RND_ROUND;
                                gnc_numeric val;
                                rowNumData = (gnc_numeric*)g_hash_table_lookup( schedule,
                                                                                &curDate );
                                if ( rowNumData == NULL ) {
                                        int j;
                                        GDate *dateKeyCopy = g_date_new();

                                        *dateKeyCopy = curDate;
                                        rowNumData = g_new0( gnc_numeric, ldd->ld.revNumPmts );
                                        g_assert( rowNumData != NULL );
                                        for ( j=0; j<ldd->ld.revNumPmts; j++ ) {
                                                rowNumData[j] = gnc_numeric_error( GNC_ERROR_ARG );
                                        }
                                        g_hash_table_insert( schedule,
                                                             (gpointer)dateKeyCopy,
                                                             (gpointer)rowNumData );
                                }
                                
                                val = double_to_gnc_numeric( (double)ldd->ld
                                                             .repayOpts[i]
                                                             ->amount,
                                                             100, gncn_how );
                                rowNumData[ ldd->ld.revRepayOptToColMap[i] ]
                                        = val;
                        }
                }
        }

        /* Convert the GHashTable into a sorted GList in the LoanData */
        {
                if ( ldd->ld.revSchedule != NULL ) {
                        g_list_foreach( ldd->ld.revSchedule,
                                        ld_rev_sched_list_free,
                                        NULL );
                        g_list_free( ldd->ld.revSchedule );
                        ldd->ld.revSchedule = NULL;
                }
                g_hash_table_foreach( schedule, ld_rev_hash_to_list,
                                      &ldd->ld.revSchedule );
                g_hash_table_foreach( schedule, ld_rev_hash_free_date_keys,
                                      NULL );
                g_hash_table_destroy( schedule );
                ldd->ld.revSchedule =
                        g_list_sort( ldd->ld.revSchedule, (GCompareFunc)g_date_compare );
        }
}

static
void
ld_rev_update_clist( LoanDruidData *ldd, GDate *start, GDate *end )
{
        static gchar *NO_AMT_CELL_TEXT = " ";
        GList *l;
        GNCPrintAmountInfo pai;
        /* '+1' for the date cell */
        gchar *rowText[ ldd->ld.revNumPmts + 1 ];

        pai = gnc_default_price_print_info();
        pai.min_decimal_places = 2;

        gtk_clist_clear( ldd->revCL );
        gtk_clist_freeze( ldd->revCL );

        for ( l = ldd->ld.revSchedule; l != NULL; l = l->next )
        {
                int i;
                gchar tmpBuf[50];
                RevRepaymentRow *rrr = (RevRepaymentRow*)l->data;

                if ( g_date_compare( &rrr->date, start ) < 0 )
                        continue;
                if ( g_date_compare( &rrr->date, end ) > 0 )
                        continue; /* though we can probably return, too. */

                qof_print_gdate( tmpBuf, MAX_DATE_LENGTH, &rrr->date );
                rowText[0] = g_strdup( tmpBuf );

                for ( i=0; i<ldd->ld.revNumPmts; i++ )
                {
                        int numPrinted;
                        if ( gnc_numeric_check( rrr->numCells[i] )
                             == GNC_ERROR_ARG )
                        {
                                rowText[i+1] = NO_AMT_CELL_TEXT;
                                continue;
                        }

                        numPrinted = xaccSPrintAmount( tmpBuf, rrr->numCells[i], pai );
                        g_assert( numPrinted < 50 );
                        rowText[i+1] = g_strdup( tmpBuf );
                }

                gtk_clist_append( ldd->revCL, rowText );

                for ( i=ldd->ld.revNumPmts-1; i>=0; i-- )
                {
                        if ( strcmp( rowText[i], NO_AMT_CELL_TEXT ) == 0 )
                                continue;
                        g_free( rowText[i] );
                }
        }
        gtk_clist_thaw( ldd->revCL );
}

static
void
ld_rev_clist_allocate_col_widths( GtkWidget *w,
                                  GtkAllocation *alloc,
                                  gpointer user_data )
{
        LoanDruidData *ldd = (LoanDruidData*)user_data;
        gint i, evenWidth, width;

        width = alloc->width;
        /* The '-10' is to account for misc widget noise not accounted for by
         * the simple division. */
        evenWidth = (gint)(width / (ldd->ld.revNumPmts+1) ) - 10;
        gtk_clist_freeze( ldd->revCL );
        for ( i=0; i<ldd->ld.revNumPmts+1; i++ )
        {
                gtk_clist_set_column_width( ldd->revCL,
                                            i, evenWidth );
        }
        gtk_clist_thaw( ldd->revCL );
}
