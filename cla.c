/* Central Legitimization Agency
 * - responds to requests from voters with a validation number
 * - track pair of (voter,validation number), don't give 1 voter
 *   more than 1 validation number
 *
 * Must listen for requests from voters. (TLS, password auth)
 * Push validation numbers to CTF. (TLS, auth as client)
 * Request results data from CTF. (PULBIC)
 * Request vote/non-vote data from CTF. (TLS, auth as client)
 */

#include "warn.h"

int main(int argc, char *argv[])
{
	if (argc != 5) {
		w_prt(
		"usage: %s <listen addr> <listen port> <ctf addr> <ctf port>\n",
			argc?argv[0]:"cla");
		return 1;
	}

	return 0;
}
