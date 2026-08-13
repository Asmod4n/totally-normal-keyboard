class Tnk
  # Encrypted-at-rest store for recorded HID macros (e.g. passwords typed
  # once on the physical keyboard, replayed later on demand). Unlocking
  # takes the raw HID report bytes captured during passphrase entry
  # (not decoded characters - see hid_proc's :unlock_input mode), so the
  # derived key never depends on which keyboard layout happens to be
  # configured.
  module Vault
    extend self

    PASSPHRASE_REPORTS = 256
    PASSPHRASE_BYTES   = PASSPHRASE_REPORTS * 8

    KDF_T_COST      = 3
    KDF_M_COST_KIB  = 65536
    KDF_PARALLELISM = 1
    KDF_TYPE        = Argon2::ID
    KDF_VERSION     = 0x13

    CANARY_CONTEXT   = "tnkvault"
    MACRO_CONTEXT    = "tnkmacro"
    CANARY_PLAINTEXT = "tnk vault canary v1"
    CANARY_BYTES     = CANARY_PLAINTEXT.bytesize + Hydro::SecretBox::HEADERBYTES

    SALT_KEY   = "salt"
    CANARY_KEY = "canary"

    def open(share_dir)
      return if @env
      dir = File.join(share_dir, "vault")
      mkdir_p(dir)
      @env = MDB::Env.new(mapsize: 16 * 1024 * 1024, maxdbs: 2)
      @env.open(dir, 0)
      @meta   = @env.database(MDB::CREATE, "meta")
      @macros = @env.database(MDB::CREATE | MDB::INTEGERKEY, "macros")
      nil
    end

    def close
      lock
      @env.close if @env
      @env = @meta = @macros = nil
    end

    def initialized?
      !!@meta[SALT_KEY]
    end

    def locked?
      @key.nil?
    end

    # padded_passphrase: exactly PASSPHRASE_BYTES bytes, raw HID report
    # bytes captured during :unlock_input, zero-padded to full length.
    # Always wiped by the time this returns (success or failure).
    def unlock(padded_passphrase)
      raise VaultError, "vault not open" unless @meta

      salt = @meta[SALT_KEY] || begin
        s = Hydro::Random.buf(16)
        @meta[SALT_KEY] = s
        s
      end

      result = do_unlock(padded_passphrase, salt, @meta[CANARY_KEY])
      return false unless result

      @meta[CANARY_KEY] = result[:canary] if result[:canary]
      @key = result[:key]
      true
    end

    def lock
      secure_wipe_memory(@key) if @key
      @key = nil
    end

    def store_macro(id, reports)
      raise VaultError, "vault locked" unless @key
      plaintext = CBOR.encode(reports)
      ciphertext = Hydro::SecretBox.encrypt(plaintext, MACRO_CONTEXT, @key, id)
      secure_wipe_memory(plaintext)
      @macros[id.to_bin] = ciphertext
      nil
    end

    def load_macro(id)
      raise VaultError, "vault locked" unless @key
      ciphertext = @macros[id.to_bin]
      return nil unless ciphertext
      plaintext = Hydro::SecretBox.decrypt(ciphertext, MACRO_CONTEXT, @key, id)
      reports = CBOR.decode(plaintext)
      secure_wipe_memory(plaintext)
      reports
    end

    private

    def mkdir_p(path)
      Dir.mkdir(path) unless File.directory?(path)
    rescue Errno::ENOENT
      parent = File.dirname(path)
      mkdir_p(parent)
      retry
    rescue Errno::EEXIST
    end

    def derive_key(padded_passphrase, salt)
      unless padded_passphrase.bytesize == PASSPHRASE_BYTES
        raise ArgumentError, "passphrase buffer must be exactly #{PASSPHRASE_BYTES} bytes"
      end

      out = Argon2.hash(padded_passphrase,
        salt:        salt,
        hashlen:     Hydro::SecretBox::KEYBYTES,
        t_cost:      KDF_T_COST,
        m_cost:      KDF_M_COST_KIB,
        parallelism: KDF_PARALLELISM,
        type:        KDF_TYPE,
        version:     KDF_VERSION)

      # Argon2.hash also hands back :encoded, which carries the raw digest in
      # base64 -- a second copy of the key that would otherwise sit in the heap
      # until the GC reuses that page. It is a single C-side allocation, so it is
      # safe to wipe. (:salt is the caller's own buffer and is not secret: leave it.)
      secure_wipe_memory(out[:encoded])

      out[:hash]
    end

    def create_canary(key)
      Hydro::SecretBox.encrypt(CANARY_PLAINTEXT, CANARY_CONTEXT, key)
    end

    def verify_canary(key, stored_canary)
      # The ciphertext length comes from storage, never from the passphrase, so
      # branching on it leaks nothing about the passphrase. A wrong length is
      # corrupt storage, not a failed unlock, and must not be reported as one.
      unless stored_canary.is_a?(String) && stored_canary.bytesize == CANARY_BYTES
        raise VaultError, "stored canary is malformed"
      end

      # Successful authenticated decryption already proves the plaintext is ours;
      # comparing it against CANARY_PLAINTEXT would only add a branch on decrypted
      # data and buy nothing.
      Hydro::SecretBox.decrypt(stored_canary, CANARY_CONTEXT, key)
      true
    rescue Hydro::SecretBox::Error
      # The only secret-dependent decision here is made inside libhydrogen's
      # constant-time MAC check; a bit-flipped canary and a wrong key both land
      # here, and everything after this point runs on an outcome that is already
      # public (the user learns "wrong passphrase" either way).
      false
    end

    def do_unlock(padded_passphrase, salt, stored_canary)
      key = derive_key(padded_passphrase, salt)

      if stored_canary.nil?
        return { key: key, canary: create_canary(key) }
      end

      if verify_canary(key, stored_canary)
        { key: key, canary: nil }
      else
        # Asymmetric with the success path by one 32-byte memset, which is fine:
        # it happens after verify_canary already returned, so it cannot feed back
        # into the constant-time part, and its duration is passphrase-independent.
        secure_wipe_memory(key)
        nil
      end
    ensure
      # Runs on every path, including an Argon2 or malformed-canary raise. Guarded
      # only so a caller-contract violation surfaces as its own error instead of
      # being masked by one from here.
      secure_wipe_memory(padded_passphrase) if padded_passphrase.is_a?(String)
    end
  end
end
