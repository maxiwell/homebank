/*  HomeBank -- Free, easy, personal accounting for everyone.
 *  Copyright (C) 1995-2016 Maxime DOYEN
 *
 *  This file is part of HomeBank.
 *
 *  HomeBank is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  HomeBank is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "homebank.h"

#include "hb-transaction.h"
#include "hb-tag.h"
#include "hb-split.h"

/****************************************************************************/
/* Debug macros										                        */
/****************************************************************************/
#define MYDEBUG 0

#if MYDEBUG
#define DB(x) (x);
#else
#define DB(x);
#endif

/* our global datas */
extern struct HomeBank *GLOBALS;
extern struct Preferences *PREFS;


/* = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = */

void
da_transaction_clean(Transaction *item)
{
	if(item != NULL)
	{
		if(item->wording != NULL)
		{
			g_free(item->wording);
			item->wording = NULL;
		}
		if(item->info != NULL)
		{
			g_free(item->info);
			item->info = NULL;
		}
		if(item->tags != NULL)
		{
			DB( g_print(" -> item->tags %p\n", item->tags) );
			g_free(item->tags);
			item->tags = NULL;
		}

		da_splits_free(item->splits);
		item->flags &= ~(OF_SPLIT); //Flag that Splits are cleared

		if(item->same != NULL)
		{
			g_list_free(item->same);
			item->same = NULL;
		}
	}
}



void
da_transaction_free(Transaction *item)
{
	if(item != NULL)
	{
		da_transaction_clean(item);
		g_free(item);
	}
}


Transaction *
da_transaction_malloc(void)
{
	return g_malloc0(sizeof(Transaction));
}


Transaction *da_transaction_copy(Transaction *src_txn, Transaction *dst_txn)
{
	DB( g_print("da_transaction_copy\n") );

	da_transaction_clean (dst_txn);

	memmove(dst_txn, src_txn, sizeof(Transaction));
	
	//duplicate the string
	dst_txn->wording = g_strdup(src_txn->wording);
	dst_txn->info = g_strdup(src_txn->info);

	//duplicate tags
	transaction_tags_clone(src_txn, dst_txn);

	if (da_splits_clone(src_txn->splits, dst_txn->splits) > 0)
		dst_txn->flags |= OF_SPLIT; //Flag that Splits are active

	return dst_txn;
}


Transaction *da_transaction_init_from_template(Transaction *txn, Archive *arc)
{
	//txn->date		= 0;
	txn->amount	= arc->amount;
	//#1258344 keep the current account if tpl is empty
	if(arc->kacc)
		txn->kacc	= arc->kacc;
	txn->paymode	= arc->paymode;
	txn->flags		= arc->flags | OF_ADDED;
	txn->status		= arc->status;
	txn->kpay		= arc->kpay;
	txn->kcat		= arc->kcat;
	txn->kxferacc	= arc->kxferacc;
	txn->wording	= g_strdup(arc->wording);
	txn->info		= NULL;
	if( da_splits_clone(arc->splits, txn->splits) > 0)
		txn->flags |= OF_SPLIT; //Flag that Splits are active

	return txn;
}


Transaction *da_transaction_clone(Transaction *src_item)
{
Transaction *new_item = g_memdup(src_item, sizeof(Transaction));

	DB( g_print("da_transaction_clone\n") );

	if(new_item)
	{
		//duplicate the string
		new_item->wording = g_strdup(src_item->wording);
		new_item->info = g_strdup(src_item->info);

		//duplicate tags
		transaction_tags_clone(src_item, new_item);
		
		if( da_splits_clone(src_item->splits, new_item->splits) > 0)
			new_item->flags |= OF_SPLIT; //Flag that Splits are active

	}
	return new_item;
}


GList *
da_transaction_new(void)
{
	return NULL;
}


guint
da_transaction_length(void)
{
GList *lst_acc, *lnk_acc;
guint count = 0;

	lst_acc = g_hash_table_get_values(GLOBALS->h_acc);
	lnk_acc = g_list_first(lst_acc);
	while (lnk_acc != NULL)
	{
	Account *acc = lnk_acc->data;
	
		count += g_queue_get_length (acc->txn_queue);
		lnk_acc = g_list_next(lnk_acc);
	}
	g_list_free(lst_acc);
	return count;
}


static void da_transaction_queue_free_ghfunc(Transaction *item, gpointer data)
{
	da_transaction_free (item);
}


void da_transaction_destroy(void)
{
GList *lacc, *list;

	lacc = g_hash_table_get_values(GLOBALS->h_acc);
	list = g_list_first(lacc);
	while (list != NULL)
	{
	Account *acc = list->data;

		g_queue_foreach(acc->txn_queue, (GFunc)da_transaction_queue_free_ghfunc, NULL);
		list = g_list_next(list);
	}
	g_list_free(lacc);
}


static gint da_transaction_compare_datafunc(Transaction *a, Transaction *b, gpointer data)
{
	return ((gint)a->date - b->date);
}


void da_transaction_queue_sort(GQueue *queue)
{
	g_queue_sort(queue, (GCompareDataFunc)da_transaction_compare_datafunc, NULL);
}


static gint da_transaction_compare_func(Transaction *a, Transaction *b)
{
	return ((gint)a->date - b->date);
}


GList *da_transaction_sort(GList *list)
{
	return( g_list_sort(list, (GCompareFunc)da_transaction_compare_func));
}


static void da_transaction_insert_memo(Transaction *item)
{
	// append the memo if new
	if( item->wording != NULL )
	{
		if( g_hash_table_lookup(GLOBALS->h_memo, item->wording) == NULL )
		{
			g_hash_table_insert(GLOBALS->h_memo, g_strdup(item->wording), NULL);
		}
	}
}


gboolean da_transaction_insert_sorted(Transaction *newitem)
{
Account *acc;
GList *lnk_txn;

	acc = da_acc_get(newitem->kacc);
	if(!acc) 
		return FALSE;
	
	lnk_txn = g_queue_peek_tail_link(acc->txn_queue);
	while (lnk_txn != NULL)
	{
	Transaction *item = lnk_txn->data;

		if(item->date <= newitem->date)
			break;
		
		lnk_txn = g_list_previous(lnk_txn);
	}

	// we're at insert point, insert after txn
	g_queue_insert_after(acc->txn_queue, lnk_txn, newitem);

	da_transaction_insert_memo(newitem);
	return TRUE;
}


// nota: this is called only when loading xml file
gboolean da_transaction_prepend(Transaction *item)
{
Account *acc;

	acc = da_acc_get(item->kacc);
	if(acc)
		item->kcur = acc->kcur;
	g_queue_push_tail(acc->txn_queue, item);
	da_transaction_insert_memo(item);
	return TRUE;
}


/* = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = */

static guint32
da_transaction_get_max_kxfer(void)
{
GList *lst_acc, *lnk_acc;
GList *list;
guint32 max_key = 0;

	DB( g_print("da_transaction_get_max_kxfer\n") );

	lst_acc = g_hash_table_get_values(GLOBALS->h_acc);
	lnk_acc = g_list_first(lst_acc);
	while (lnk_acc != NULL)
	{
	Account *acc = lnk_acc->data;

		list = g_queue_peek_head_link(acc->txn_queue);
		while (list != NULL)
		{
		Transaction *item = list->data;

			if( item->paymode == PAYMODE_INTXFER )
			{
				max_key = MAX(max_key, item->kxfer);
			}
			list = g_list_next(list);
		}
		
		lnk_acc = g_list_next(lnk_acc);
	}
	g_list_free(lst_acc);

	DB( g_print(" max_key : %d \n", max_key) );

	return max_key;
}


static void da_transaction_goto_orphan(Transaction *txn)
{
const gchar *oatn = "orphaned transactions";
Account *acc;

	acc = da_acc_get_by_name((gchar *)oatn);
	if(acc == NULL)
	{
		acc = da_acc_malloc();
		acc->name = g_strdup(oatn);
		da_acc_append(acc);
	}
	txn->kacc = acc->key;
}


void da_transaction_consistency(Transaction *item)
{
Account *acc;
Category *cat;
Payee *pay;
gint nbsplit;

	// ensure date is between range
	item->date = CLAMP(item->date, HB_MINDATE, HB_MAXDATE);

	// check account exists
	acc = da_acc_get(item->kacc);
	if(acc == NULL)
	{
		g_warning("txn consistency: fixed invalid acc %d", item->kacc);
		da_transaction_goto_orphan(item);
		GLOBALS->changes_count++;
	}

	// check category exists
	cat = da_cat_get(item->kcat);
	if(cat == NULL)
	{
		g_warning("txn consistency: fixed invalid cat %d", item->kcat);
		item->kcat = 0;
		GLOBALS->changes_count++;
	}

	// check split category #1340142
	split_cat_consistency(item->splits);

	//# 1416624 empty category when split
	nbsplit = da_splits_count(item->splits);
	if(nbsplit > 0 && item->kcat > 0)
	{
		g_warning("txn consistency: fixed invalid cat on split txn");
		item->kcat = 0;
		GLOBALS->changes_count++;
	}
	
	// check payee exists
	pay = da_pay_get(item->kpay);
	if(pay == NULL)
	{
		g_warning("txn consistency: fixed invalid pay %d", item->kpay);
		item->kpay = 0;
		GLOBALS->changes_count++;
	}

	// reset dst acc for non xfer transaction
	if( item->paymode != PAYMODE_INTXFER )
		item->kxferacc = 0;

	//#1295877 ensure income flag is correctly set
	item->flags &= ~(OF_INCOME);
	if( item->amount > 0)
		item->flags |= (OF_INCOME);

	//#1308745 ensure remind flag unset if reconciled
	//useless since 5.0
	//if( item->flags & OF_VALID )
	//	item->flags &= ~(OF_REMIND);


	// check child xfer #1464961
	//remove for 5.0.5: this cause some trouble to some people
	//so let's keep the data insane...
	/*if( item->paymode == PAYMODE_INTXFER )
	{
	Transaction *child;
	GList *matchlist;
	gint count;
	
		child = transaction_xfer_child_strong_get (item);
		if(child == NULL)
		{
			matchlist = transaction_xfer_child_might_list_get(item);
			count = g_list_length (matchlist);
			if( count == 0 )
			{
				transaction_xfer_create_child(item, NULL);
				g_warning("txn consistency: fixed invalid child xfer");
			}
			else
			{
				item->paymode = PAYMODE_XFER;
				item->kxfer = 0;
				item->kxferacc = 0;
			}
			GLOBALS->changes_count++;
			g_list_free(matchlist);
		}
	}*/

}


/* = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = */
/* new transfer functions */

//todo: add strong control and extend to payee, maybe memo ?
/*static gboolean transaction_is_duplicate(Transaction *stxn, Transaction *dtxn, gint daygap)
{
gboolean retval = FALSE;

	if(stxn == dtxn)
		return FALSE;

	if( (dtxn->amount == stxn->amount) && (dtxn->kacc == stxn->kacc) &&
		(dtxn->date <= (stxn->date + daygap)) && (dtxn->date >= (stxn->date - daygap)) 
		// add pos control here too ?
	  )
	{
		retval = TRUE;
	}
	return retval;
}*/

static void transaction_xfer_create_child(Transaction *ope, GtkWidget *treeview)
{
Transaction *child;
Account *acc;
gchar swap;

	DB( g_print("\n[transaction] transaction_xfer_create_child\n") );
	
	if( ope->kxferacc > 0 )
	{
		child = da_transaction_clone(ope);

		child->amount = -child->amount;
		child->flags ^= (OF_INCOME);	// invert flag
		//#1268026
		child->status = TXN_STATUS_NONE;
		//child->flags &= ~(OF_VALID);	// delete reconcile state

		swap = child->kacc;
		child->kacc = child->kxferacc;
		child->kxferacc = swap;

		/* update acc flags */
		acc = da_acc_get( child->kacc );
		if(acc != NULL)
		{
			acc->flags |= AF_ADDED;

			//strong link
			guint maxkey = da_transaction_get_max_kxfer();

			DB( g_print(" + maxkey is %d\n", maxkey) );


			ope->kxfer = maxkey+1;
			child->kxfer = maxkey+1;

			DB( g_print(" + strong link to %d\n", ope->kxfer) );


			DB( g_print(" + add transfer, %p\n", child) );

			da_transaction_insert_sorted(child);

			account_balances_add (child);

			if(treeview != NULL)
				transaction_add_treeview(child, treeview, ope->kacc);
		}
	}

}


//todo: add strong control and extend to payee, maybe memo ?
static gboolean transaction_xfer_child_might(Transaction *stxn, Transaction *dtxn, gint daygap)
{
gboolean retval = FALSE;

	if(stxn == dtxn)
		return FALSE;

	if( stxn->kcur == dtxn->kcur &&
	    stxn->date == dtxn->date &&
	    stxn->kxferacc == dtxn->kacc &&
	    ABS(stxn->amount) == ABS(dtxn->amount) &&
	    dtxn->kxfer == 0)
	{
		retval = TRUE;
	}
	return retval;
}


static GList *transaction_xfer_child_might_list_get(Transaction *ope)
{
GList *lst_acc, *lnk_acc;
GList *list, *matchlist = NULL;

	DB( g_print("\n[transaction]xfer_get_potential_child\n") );

	lst_acc = g_hash_table_get_values(GLOBALS->h_acc);
	lnk_acc = g_list_first(lst_acc);
	while (lnk_acc != NULL)
	{
	Account *acc = lnk_acc->data;

		if( !(acc->flags & AF_CLOSED) && (acc->key != ope->kacc) )
		{
			list = g_queue_peek_tail_link(acc->txn_queue);
			while (list != NULL)
			{
			Transaction *item = list->data;
	
				// no need to go higher than src txn date
				if(item->date < ope->date)
					break;
		
				if( transaction_xfer_child_might(ope, item, 0) == TRUE )
				{
					//DB( g_print(" - match : %d %s %f %d=>%d\n", item->date, item->wording, item->amount, item->account, item->kxferacc) );
					matchlist = g_list_append(matchlist, item);
				}
				list = g_list_previous(list);
			}
		}
		
		lnk_acc = g_list_next(lnk_acc);
	}
	g_list_free(lst_acc);

	return matchlist;
}


void transaction_xfer_search_or_add_child(Transaction *ope, GtkWidget *treeview)
{
GList *matchlist;
gint count;

	DB( g_print("\n[transaction] transaction_xfer_search_or_add_child\n") );

	matchlist = transaction_xfer_child_might_list_get(ope);
	count = g_list_length(matchlist);

	DB( g_print(" - found result is %d, switching\n", count) );

	switch(count)
	{
		case 0:		//we should create the child
			transaction_xfer_create_child(ope, treeview);
			break;

		//todo: maybe with just 1 match the user must choose ?
		//#942346: bad idea so to no let the user confirm, so let him confirm
		/*
		case 1:		//transform the transaction to a child transfer
		{
			GList *list = g_list_first(matchlist);
			transaction_xfer_change_to_child(ope, list->data);
			break;
		}
		*/

		default:	//the user must choose himself
		{
		Transaction *child;

			child = ui_dialog_transaction_xfer_select_child(treeview, matchlist);
			if(child == NULL)
				transaction_xfer_create_child(ope, treeview);
			else
				transaction_xfer_change_to_child(ope, child);
			break;
		}
	}

	g_list_free(matchlist);

}


Transaction *transaction_xfer_child_strong_get(Transaction *src)
{
Account *acc;
GList *list;

	DB( g_print("\n[transaction] transaction_xfer_child_strong_get\n") );

	acc = da_acc_get(src->kxferacc);
	if( !acc )
		return NULL;

	DB( g_print(" - search: %d %s %f %d=>%d - %d\n", src->date, src->wording, src->amount, src->kacc, src->kxferacc, src->kxfer) );

	list = g_queue_peek_tail_link(acc->txn_queue);
	while (list != NULL)
	{
	Transaction *item = list->data;

		//#1252230
		//if( item->paymode == PAYMODE_INTXFER && item->kacc == src->kxferacc && item->kxfer == src->kxfer )
		if( item->paymode == PAYMODE_INTXFER 
		 && item->date == src->date //added in 5.1
		 && item->kxfer == src->kxfer 
		 && item != src )
		{
			DB( g_print(" - found : %d %s %f %d=>%d - %d\n", item->date, item->wording, item->amount, item->kacc, item->kxferacc, src->kxfer) );
			return item;
		}
		list = g_list_previous(list);
	}
	DB( g_print(" - not found...\n") );
	return NULL;
}




void transaction_xfer_change_to_child(Transaction *ope, Transaction *child)
{
Account *dstacc;

	DB( g_print("\n[transaction] transaction_xfer_change_to_child\n") );

	if(ope->kcur != child->kcur)
		return;

	child->paymode = PAYMODE_INTXFER;

	ope->kxferacc = child->kacc;
	child->kxferacc = ope->kacc;

	/* update acc flags */
	dstacc = da_acc_get( child->kacc);
	if(dstacc != NULL)
		dstacc->flags |= AF_CHANGED;

	//strong link
	guint maxkey = da_transaction_get_max_kxfer();
	ope->kxfer = maxkey+1;
	child->kxfer = maxkey+1;

}


void transaction_xfer_sync_child(Transaction *s_txn, Transaction *child)
{

	DB( g_print("\n[transaction] transaction_xfer_sync_child\n") );

	account_balances_sub (child);

	child->date		= s_txn->date;
	child->amount   = -s_txn->amount;
	child->flags	= child->flags | OF_CHANGED;
	//#1295877
	child->flags &= ~(OF_INCOME);
	if( child->amount > 0)
	  child->flags |= (OF_INCOME);
	child->kpay		= s_txn->kpay;
	child->kcat		= s_txn->kcat;
	if(child->wording)
		g_free(child->wording);
	child->wording	= g_strdup(s_txn->wording);
	if(child->info)
		g_free(child->info);
	child->info		= g_strdup(s_txn->info);

	//#1252230 sync account also
	child->kacc		= s_txn->kxferacc;
	child->kxferacc = s_txn->kacc;

	account_balances_add (child);
	
	//synchronise tags since 5.1
	if(child->tags)
		g_free(child->tags);
	transaction_tags_clone (s_txn, child);

}


void transaction_xfer_remove_child(Transaction *src)
{
Transaction *dst;

	DB( g_print("\n[transaction] transaction_xfer_remove_child\n") );

	dst = transaction_xfer_child_strong_get( src );

	DB( g_print(" -> return is %s, %p\n", dst->wording, dst) );

	if( dst != NULL )
	{
	Account *acc = da_acc_get(dst->kacc);

		DB( g_print("deleting...") );
		src->kxfer = 0;
		src->kxferacc = 0;
		account_balances_sub(dst);
		g_queue_remove(acc->txn_queue, dst);
		da_transaction_free (dst);
	}
}


// still useful for upgrade from < file v0.6 (hb v4.4 kxfer)
Transaction *transaction_old_get_child_transfer(Transaction *src)
{
Account *acc;
GList *list;

	DB( g_print("\n[transaction] transaction_get_child_transfer\n") );

	//DB( g_print(" search: %d %s %f %d=>%d\n", src->date, src->wording, src->amount, src->account, src->kxferacc) );
	acc = da_acc_get(src->kxferacc);

	list = g_queue_peek_head_link(acc->txn_queue);
	while (list != NULL)
	{
	Transaction *item = list->data;

		// no need to go higher than src txn date
		if(item->date > src->date)
			break;

		if( item->paymode == PAYMODE_INTXFER)
		{
			if( src->date == item->date &&
			    src->kacc == item->kxferacc &&
			    src->kxferacc == item->kacc &&
			    ABS(src->amount) == ABS(item->amount) )
			{
				//DB( g_print(" found : %d %s %f %d=>%d\n", item->date, item->wording, item->amount, item->account, item->kxferacc) );

				return item;
			}
		}
		list = g_list_next(list);
	}

	DB( g_print(" not found...\n") );

	return NULL;
}


/* = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = */


void transaction_add(Transaction *ope, GtkWidget *treeview, guint32 accnum)
{
Transaction *newope;
Account *acc;

	DB( g_print("\n[transaction] transaction add\n") );

	//controls accounts valid (archive scheduled maybe bad)
	acc = da_acc_get(ope->kacc);
	if(acc == NULL) return;

	ope->kcur = acc->kcur;
	
	if(ope->paymode == PAYMODE_INTXFER)
	{
		acc = da_acc_get(ope->kxferacc);
		if(acc == NULL) return;
		
		// delete any splits
		da_splits_free(ope->splits);
		ope->flags &= ~(OF_SPLIT); //Flag that Splits are cleared
	}

	//allocate a new entry and copy from our edited structure
	newope = da_transaction_clone(ope);

	//init flag and keep remind status
	// already done in deftransaction_get
	//ope->flags |= (OF_ADDED);
	//remind = (ope->flags & OF_REMIND) ? TRUE : FALSE;
	//ope->flags &= (~OF_REMIND);

	/* cheque number is already stored in deftransaction_get */
	/* todo:move this to transaction add
		store a new cheque number into account ? */

	if( (newope->paymode == PAYMODE_CHECK) && (newope->info) && !(newope->flags & OF_INCOME) )
	{
	guint cheque;

		/* get the active account and the corresponding cheque number */
		acc = da_acc_get( newope->kacc);
		cheque = atol(newope->info);

		//DB( g_print(" -> should store cheque number %d to %d", cheque, newope->account) );
		if( newope->flags & OF_CHEQ2 )
		{
			acc->cheque2 = MAX(acc->cheque2, cheque);
		}
		else
		{
			acc->cheque1 = MAX(acc->cheque1, cheque);
		}
	}

	/* add normal transaction */
	acc = da_acc_get( newope->kacc);
	if(acc != NULL)
	{
		acc->flags |= AF_ADDED;

		DB( g_print(" + add normal %p\n", newope) );
		//da_transaction_append(newope);
		da_transaction_insert_sorted(newope);

		if(treeview != NULL)
			transaction_add_treeview(newope, treeview, accnum);

		account_balances_add(newope);

		if(newope->paymode == PAYMODE_INTXFER)
		{
			transaction_xfer_search_or_add_child(newope, treeview);
		}
	}
}


void transaction_add_treeview(Transaction *ope, GtkWidget *treeview, guint32 accnum)
{
GtkTreeModel *model;
GtkTreeIter  iter;
//GtkTreePath *path;
//GtkTreeSelection *sel;

	DB( g_print("\n[transaction] transaction add treeview\n") );

	if(ope->kacc == accnum)
	{
		model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));
		gtk_list_store_append (GTK_LIST_STORE(model), &iter);

		gtk_list_store_set (GTK_LIST_STORE(model), &iter,
				LST_DSPOPE_DATAS, ope,
				-1);

		//activate that new line
		//path = gtk_tree_model_get_path(model, &iter);
		//gtk_tree_view_expand_to_path(GTK_TREE_VIEW(treeview), path);

		//sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
		//gtk_tree_selection_select_iter(sel, &iter);

		//gtk_tree_path_free(path);

	}
}


/* = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = */


static gboolean misc_text_match(gchar *text, gchar *searchtext, gboolean exact)
{
gboolean match = FALSE;

	if(text == NULL)
		return FALSE;
	
	//DB( g_print("search %s in %s\n", rul->name, ope->wording) );
	if( searchtext != NULL )
	{
		if( exact == TRUE )
		{
			if( g_strrstr(text, searchtext) != NULL )
			{
				DB( g_print(" found case '%s'\n", searchtext) );
				match = TRUE;
			}
		}
		else
		{
		gchar *word   = g_utf8_casefold(text, -1);
		gchar *needle = g_utf8_casefold(searchtext, -1);

			if( g_strrstr(word, needle) != NULL )
			{
				DB( g_print(" found nocase '%s'\n", searchtext) );
				match = TRUE;
			}
			g_free(word);
			g_free(needle);
		}
	}

	return match;
}

static gboolean misc_regex_match(gchar *text, gchar *searchtext, gboolean exact)
{
gboolean match = FALSE;

	if(text == NULL)
		return FALSE;
	
	DB( g_print("match RE %s in %s\n", searchtext, text) );
	if( searchtext != NULL )
	{
		match = g_regex_match_simple(searchtext, text, ((exact == TRUE)?0:G_REGEX_CASELESS) | G_REGEX_OPTIMIZE, G_REGEX_MATCH_NOTEMPTY );
		if (match == TRUE) { DB( g_print(" found pattern '%s'\n", searchtext) ); }
	}
	return match;
}


static Assign *transaction_auto_assign_eval_txn(GList *l_rul, Transaction *txn)
{
Assign *rule = NULL;
GList *list;
	
	DB( g_print("- eval every rules, and return the last that match\n") );

	list = g_list_first(l_rul);
	while (list != NULL)
	{
	Assign *rul = list->data;
	gchar *text;

		text = txn->wording;
		if(rul->field == 1) //payee
		{
		Payee *pay = da_pay_get(txn->kpay);
			if(pay)
				text = pay->name;
		}
		
		if( !(rul->flags & ASGF_REGEX) )
		{
			if( misc_text_match(text, rul->text, rul->flags & ASGF_EXACT) )
				rule = rul;
		}
		else
		{
			if( misc_regex_match(text, rul->text, rul->flags & ASGF_EXACT) )
				rule = rul;
		}

		list = g_list_next(list);
	}

	return rule;
}


static Assign *transaction_auto_assign_eval(GList *l_rul, gchar *text)
{
Assign *rule = NULL;
GList *list;
	
	DB( g_print("- eval every rules, and return the last that match\n") );

	list = g_list_first(l_rul);
	while (list != NULL)
	{
	Assign *rul = list->data;

		if( rul->field == 0 )   //memo
		{
			if( !(rul->flags & ASGF_REGEX) )
			{
				if( misc_text_match(text, rul->text, rul->flags & ASGF_EXACT) )
					rule = rul;
			}
			else
			{
				if( misc_regex_match(text, rul->text, rul->flags & ASGF_EXACT) )
					rule = rul;
			}
		}
		list = g_list_next(list);
	}

	return rule;
}


gint transaction_auto_assign(GList *ope_list, guint32 kacc)
{
GList *l_ope;
GList *l_rul;
gint changes = 0;

	DB( g_print("\n[transaction] transaction_auto_assign\n") );

	l_rul = g_hash_table_get_values(GLOBALS->h_rul);

	l_ope = g_list_first(ope_list);
	while (l_ope != NULL)
	{
	Transaction *ope = l_ope->data;
	gboolean changed = FALSE; 

		DB( g_print("- eval ope '%s' : acc=%d, pay=%d, cat=%d\n", ope->wording, ope->kacc, ope->kpay, ope->kcat) );

		//#1215521: added kacc == 0
		if( (kacc == ope->kacc || kacc == 0) )
		{
		Assign *rul;

			rul = transaction_auto_assign_eval_txn(l_rul, ope);
			if( rul != NULL )
			{
				if( (ope->kpay == 0 || (rul->flags & ASGF_OVWPAY)) && (rul->flags & ASGF_DOPAY) )
				{
					if(ope->kpay != rul->kpay) { changed = TRUE; }
					ope->kpay = rul->kpay;
				}

				if( !(ope->flags & OF_SPLIT) )
				{
					if( (ope->kcat == 0 || (rul->flags & ASGF_OVWCAT)) && (rul->flags & ASGF_DOCAT) )
					{
						if(ope->kcat != rul->kcat) { changed = TRUE; }
						ope->kcat = rul->kcat;
					}
				}

				if( (ope->paymode == 0 || (rul->flags & ASGF_OVWMOD)) && (rul->flags & ASGF_DOMOD) )
				{
					//ugly hack - don't allow modify intxfer
					if(ope->paymode != PAYMODE_INTXFER && rul->paymode != PAYMODE_INTXFER) 
					{
						if(ope->paymode != rul->paymode) { changed = TRUE; }
						ope->paymode = rul->paymode;
					}
				}

			}

			if( ope->flags & OF_SPLIT )
			{
			guint i, nbsplit = da_splits_count(ope->splits);

				for(i=0;i<nbsplit;i++)
				{
				Split *split = ope->splits[i];
					
					DB( g_print("- eval split '%s'\n", split->memo) );

					rul = transaction_auto_assign_eval(l_rul, split->memo);
					if( rul != NULL )
					{
						//#1501144: check if user wants to set category in rule
						if( (split->kcat == 0 || (rul->flags & ASGF_OVWCAT)) && (rul->flags & ASGF_DOCAT) )
						{
							if(split->kcat != rul->kcat) { changed = TRUE; }
							split->kcat = rul->kcat;
						}
					}
				}
			}

			if(changed == TRUE)
			{
				ope->flags |= OF_CHANGED;
				changes++;
			}
		}

		l_ope = g_list_next(l_ope);
	}

	g_list_free(l_rul);

	return changes;
}


/* = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = */


guint
transaction_tags_count(Transaction *ope)
{
guint count = 0;
guint32 *ptr = ope->tags;

	DB( g_print("(transaction_tags_count)\n") );
	
	if( ope->tags == NULL )
		return 0;

	while(*ptr++ != 0 && count < 32)
		count++;

	return count;
}


void transaction_tags_clone(Transaction *src_txn, Transaction *dst_txn)
{
guint count;

	dst_txn->tags = NULL;
	count = transaction_tags_count(src_txn);
	if(count > 0)
	{
		//1501962: we must also copy the final 0
		dst_txn->tags = g_memdup(src_txn->tags, (count+1)*sizeof(guint32));
	}
}

guint
transaction_tags_parse(Transaction *ope, const gchar *tagstring)
{
gchar **str_array;
guint count, i;
Tag *tag;

	DB( g_print("(transaction_tags_parse)\n") );

	DB( g_print(" - tagstring='%s'\n", tagstring) );

	str_array = g_strsplit (tagstring, " ", 0);
	count = g_strv_length( str_array );

	g_free(ope->tags);
	ope->tags = NULL;

	DB( g_print(" -> reset storage %p\n", ope->tags) );


	if( count > 0 )
	{

		ope->tags = g_new0(guint32, count + 1);

		DB( g_print(" -> storage %p\n", ope->tags) );

		for(i=0;i<count;i++)
		{
			tag = da_tag_get_by_name(str_array[i]);
			if(tag == NULL)
			{
			Tag *newtag = da_tag_malloc();

				newtag->name = g_strdup(str_array[i]);
				da_tag_append(newtag);
				tag = da_tag_get_by_name(str_array[i]);
			}

			DB( g_print(" -> storing %d=>%s at tags pos %d\n", tag->key, tag->name, i) );

			ope->tags[i] = tag->key;
		}
		ope->tags[i] = 0;
	}

	//hex_dump(ope->tags, sizeof(guint32*)*count+1);

	g_strfreev (str_array);

	return count;
}

gchar *
transaction_tags_tostring(Transaction *ope)
{
guint count, i;
gchar **str_array;
gchar *tagstring;
Tag *tag;

	DB( g_print("transaction_tags_tostring\n") );

	DB( g_print(" -> tags at=%p\n", ope->tags) );

	if( ope->tags == NULL )
	{

		return NULL;
	}
	else
	{
		count = transaction_tags_count(ope);

		DB( g_print(" -> tags at=%p, nbtags=%d\n", ope->tags, count) );

		str_array = g_new0(gchar*, count+1);

		DB( g_print(" -> str_array at %p\n", str_array) );

		//hex_dump(ope->tags, sizeof(guint32*)*(count+1));

		for(i=0;i<count;i++)
		{
			DB( g_print(" -> try to get tag %d\n", ope->tags[i]) );

			tag = da_tag_get(ope->tags[i]);
			if( tag )
			{
				DB( g_print(" -> get %s at %d\n", tag->name, i) );
				str_array[i] = tag->name;
			}
			else
				str_array[i] = NULL;


		}

		tagstring = g_strjoinv(" ", str_array);

		g_free (str_array);

	}

	return tagstring;
}

